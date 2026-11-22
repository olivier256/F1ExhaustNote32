#include "render.h"

#include "fixed.h"
#include "framebuffer.h"
#include "vdp.h"

/* Code froid : la préparation des lignes, faite une fois au démarrage.
   Sur SH2, il est rangé à part (section .text.sdram) : l'édition de liens
   le laisse en SDRAM, et réserve la RAM interne au code de chaque image
   (projection et choix des lignes ; voir tools/sh2cycles/bench_ram.ld). */
#if defined(__sh__)
#define COLD_CODE __attribute__((noinline, section(".text.sdram")))
#else
#define COLD_CODE __attribute__((noinline))
#endif

/* Couleur 32X d'après ses composantes sur 8 bits */
#define RGB5(r, g, b) (((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10))

/* Ciel : du bleu soutenu en haut au bleu pâle vers l'horizon ; puis le
   sol vu au-delà d'une crête */
const uint16_t sky_palette[SKY_BANDS + 1] = {
    RGB5(40, 88, 200),   RGB5(50, 100, 206),  RGB5(60, 112, 212),
    RGB5(72, 124, 218),  RGB5(84, 136, 222),  RGB5(96, 148, 226),
    RGB5(108, 160, 230), RGB5(120, 172, 234), RGB5(132, 182, 238),
    RGB5(144, 192, 242), RGB5(156, 200, 244), RGB5(168, 208, 248),
    RGB5(16, 112, 32), /* sol */
};

/* Dans l'ordre de COLOR_GRASS, COLOR_RUMBLE, COLOR_ROAD, COLOR_LANE */
const uint16_t road_light[ROAD_COLORS] = {
    RGB5(24, 168, 32),   /* herbe claire */
    RGB5(240, 240, 240), /* bordure blanche */
    RGB5(112, 112, 120), /* bitume clair */
    RGB5(232, 232, 232), /* ligne blanche */
};
const uint16_t road_dark[ROAD_COLORS] = {
    RGB5(16, 144, 24), /* herbe sombre */
    RGB5(208, 24, 24), /* bordure rouge */
    RGB5(96, 96, 104), /* bitume sombre */
    RGB5(96, 96, 104), /* pas de ligne : le bitume */
};

/* ---- Une route -------------------------------------------------------------
 */

/* Colonne, relative au centre, d'un écart 16.16 au centre, arrondi */
static inline int relColumn(int32_t v) { return (v + 0x8000) >> 16; }

#define ROAD_EDGES 8

/* Bords d'une route de demi-largeur w, relatifs à son centre : bordure,
   bitume, ligne, bitume, ligne, bitume, bordure, herbe (fin de chaque
   plage ; la première plage, l'herbe, va jusqu'à edges[0]) */
COLD_CODE static void roadEdges(int32_t w, int edges[ROAD_EDGES]) {
    const uint32_t uw = (uint32_t)w; /* w >= 0 : décalages non signés */
    const int32_t rumble = (int32_t)(uw >> RUMBLE_SHIFT);
    const int32_t lane = (int32_t)((uint64_t)uw * LANE_POS_MUL >> 8);
    const int32_t half = (int32_t)(uw >> LANE_HALF_SHIFT);
    edges[0] = relColumn(-w - rumble);
    edges[1] = relColumn(-w);
    edges[2] = relColumn(-lane - half);
    edges[3] = relColumn(-lane + half);
    edges[4] = relColumn(lane - half);
    edges[5] = relColumn(lane + half);
    edges[6] = relColumn(w);
    edges[7] = relColumn(w + rumble);
}

static const uint8_t roadColors[ROAD_EDGES + 1] = {
    COLOR_GRASS, COLOR_RUMBLE, COLOR_ROAD,   COLOR_LANE, COLOR_ROAD,
    COLOR_LANE,  COLOR_ROAD,   COLOR_RUMBLE, COLOR_GRASS};

COLD_CODE uint8_t road_pixel(int32_t w, int d) {
    int edges[ROAD_EDGES];
    roadEdges(w, edges);
    int i = 0;
    while (i < ROAD_EDGES && d >= edges[i])
        i++;
    return roadColors[i];
}

/* ---- Plan des lignes préparées ----------------------------------------------
   Après la table de lignes : herbe, route 0, herbe, route 1, ..., route
   127, herbe ; puis les lignes du ciel et du sol (paires, 320 octets). */

COLD_CODE void render_layout(RoadLayout *layout) {
    uint32_t pos = FB_LINE_TABLE_BYTES;
    layout->grass = pos;
    for (unsigned k = 0; k < ROAD_WIDTHS; k++) {
        int edges[ROAD_EDGES];
        roadEdges(road_half_width(k), edges);
        pos += GRASS_MARGIN;
        layout->left[k] = pos;
        layout->center[k] = pos - (uint32_t)edges[0]; /* edges[0] <= 0 */
        layout->right[k] = layout->center[k] + (uint32_t)edges[7];
        pos = layout->right[k];
    }
    pos += GRASS_MARGIN;
    pos = (pos + 1) & ~1u;
    for (unsigned b = 0; b <= SKY_BANDS; b++, pos += SCREEN_WIDTH)
        layout->sky[b] = pos;
    layout->end = pos;
}

/* Couleur de l'octet 'offset' des lignes préparées */
COLD_CODE static uint8_t preparedPixel(const RoadLayout *layout,
                                       uint32_t offset) {
    if (offset >= layout->sky[0])
        return (uint8_t)(COLOR_SKY_FIRST +
                         (offset - layout->sky[0]) / SCREEN_WIDTH);
    for (unsigned k = 0; k < ROAD_WIDTHS; k++)
        if (offset >= layout->left[k] && offset < layout->right[k])
            return road_pixel(road_half_width(k),
                              (int)offset - (int)layout->center[k]);
    return COLOR_GRASS;
}

/* Fait une fois par frame buffer, au démarrage : rien ne presse (une
   division et une recherche par pixel) */
COLD_CODE int render_prepare(const RoadLayout *layout) {
    if (layout->end > VDP_DRAM_SIZE)
        return 0;
    for (uint32_t o = FB_LINE_TABLE_BYTES; o < layout->end; o += 2)
        vdp_dram_write16(o, (uint16_t)(preparedPixel(layout, o) << 8 |
                                       preparedPixel(layout, o + 1)));
    return 1;
}

/* ---- Choix d'une ligne -----------------------------------------------------
 */

unsigned render_line(int y, const RoadLine *line, const RoadLayout *layout,
                     RenderCost *cost) {
    uint32_t start; /* octet montré au pixel 0 de l'écran */
    unsigned attributes = 0;

    if (line->type == LINE_ROAD) {
        /* Route k : la demi-largeur la plus proche (w >= 0) */
        uint32_t k = (uint32_t)(((uint64_t)(uint32_t)line->w * ROAD_WIDTH_INV +
                                 0x80000000u) >>
                                32);
        if (k >= ROAD_WIDTHS)
            k = ROAD_WIDTHS - 1;
        const int32_t center = (line->x + 0x8000) >> 16; /* pixel d'écran */
        start = layout->center[k] - (uint32_t)center;
        /* Route entièrement hors de l'écran : de l'herbe seule (au-delà,
           la fenêtre atteindrait la route voisine) */
        if ((int32_t)(start - layout->right[k]) >= 0 ||
            (int32_t)(start + SCREEN_WIDTH - layout->left[k]) <= 0)
            start = layout->grass;
        if (line->flags & SEGMENT_DARK)
            attributes |= LINE_DARK;
        COST_ADD(cost, roadLines, 1);
    } else {
        const unsigned band = (unsigned)y >> SKY_BAND_SHIFT;
        start = layout->sky[band < SKY_BANDS ? band : SKY_BANDS];
        COST_ADD(cost, skyLines, 1);
    }

    /* La table ne donne que des mots : octet impair -> octet d'avant, et
       ligne décalée d'un point */
    attributes |= start & 1;
    vdp_dram_write16((uint32_t)y * 2, (uint16_t)((start - (start & 1)) >> 1));
    return attributes;
}
