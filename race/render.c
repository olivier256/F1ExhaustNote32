#include "render.h"

#include "bigendian.h"
#include "fixed.h"
#include "vdp.h"

/* Couleur 32X d'après ses composantes sur 8 bits */
#define RGB5(r, g, b) (((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10))

const uint16_t road_palette[ROAD_COLOR_COUNT] = {
    RGB5(24, 168, 32),   /* herbe claire */
    RGB5(16, 144, 24),   /* herbe sombre */
    RGB5(240, 240, 240), /* bordure blanche */
    RGB5(208, 24, 24),   /* bordure rouge */
    RGB5(112, 112, 120), /* bitume clair */
    RGB5(96, 96, 104),   /* bitume sombre */
    RGB5(232, 232, 232), /* ligne */
};

/* Une ligne de route : ROAD_RUNS plages, de gauche à droite */
#define ROAD_RUNS 9

/* Colonne d'écran d'une abscisse 16.16, arrondie, dans [0, SCREEN_WIDTH] */
static inline int column(int32_t x) {
    const int c = (x + 0x8000) >> 16;
    return c < 0 ? 0 : (c > SCREEN_WIDTH ? SCREEN_WIDTH : c);
}

/* Écrit une ligne faite de plages de couleur unie : la plage i va de la
   fin de la précédente (0 pour la première) à ends[i] exclu ; les fins
   sont croissantes, la dernière vaut SCREEN_WIDTH. Chaque mot est écrit
   une fois : un mot à cheval sur deux plages attend la couleur de son
   second pixel (left : celle du premier). */
static void writeRuns(uint32_t offset, const int *ends, const uint8_t *colors,
                      int count, RenderCost *cost) {
    int x = 0;
    unsigned left = 0;
    for (int i = 0; i < count; i++) {
        const int end = ends[i];
        if (end <= x)
            continue; /* plage vide */
        const unsigned c = colors[i];
        if (x & 1) { /* fin du mot commencé par la plage précédente */
            vdp_dram_write16(offset + (uint32_t)x - 1,
                             (uint16_t)(left << 8 | c));
            x++;
        }
        const uint16_t pair = (uint16_t)(c << 8 | c);
        for (; x + 2 <= end; x += 2)
            vdp_dram_write16(offset + (uint32_t)x, pair);
        if (x < end) { /* un pixel de plus : le début d'un mot */
            left = c;
            x++;
        }
        COST_ADD(cost, runs, 1);
    }
}

static void renderRoad(uint32_t offset, const RoadLine *l, RenderCost *cost) {
    /* w >= 0 : décalages non signés (sur SH2, un décalage arithmétique
       de plus de quelques bits appelle libgcc) */
    const int32_t x = l->x, w = l->w;
    const uint32_t uw = (uint32_t)w;
    const int32_t rumble = (int32_t)(uw >> RUMBLE_SHIFT);
    const int32_t lane = (int32_t)((uint64_t)uw * LANE_POS_MUL >> 8);
    const int32_t laneHalf = (int32_t)(uw >> LANE_HALF_SHIFT);

    int ends[ROAD_RUNS];
    ends[0] = column(x - w - rumble);      /* herbe */
    ends[1] = column(x - w);               /* bordure */
    ends[2] = column(x - lane - laneHalf); /* bitume */
    ends[3] = column(x - lane + laneHalf); /* ligne */
    ends[4] = column(x + lane - laneHalf); /* bitume */
    ends[5] = column(x + lane + laneHalf); /* ligne */
    ends[6] = column(x + w);               /* bitume */
    ends[7] = column(x + w + rumble);      /* bordure */
    ends[8] = SCREEN_WIDTH;                /* herbe */

    static const uint8_t light[ROAD_RUNS] = {
        COLOR_GRASS_LIGHT, COLOR_RUMBLE_LIGHT, COLOR_ROAD_LIGHT,
        COLOR_LANE,        COLOR_ROAD_LIGHT,   COLOR_LANE,
        COLOR_ROAD_LIGHT,  COLOR_RUMBLE_LIGHT, COLOR_GRASS_LIGHT};
    static const uint8_t dark[ROAD_RUNS] = {
        COLOR_GRASS_DARK, COLOR_RUMBLE_DARK, COLOR_ROAD_DARK,
        COLOR_ROAD_DARK,  COLOR_ROAD_DARK,   COLOR_ROAD_DARK,
        COLOR_ROAD_DARK,  COLOR_RUMBLE_DARK, COLOR_GRASS_DARK};

    writeRuns(offset, ends, (l->flags & SEGMENT_DARK) ? dark : light, ROAD_RUNS,
              cost);
    COST_ADD(cost, roadLines, 1);
}

static int renderSky(uint32_t offset, const Bitmap *sky, unsigned skyX, int y,
                     RenderCost *cost) {
    COST_ADD(cost, skyLines, 1);

    if (y >= sky->height) { /* sous le décor : sa couleur de sol */
        static const int end = SCREEN_WIDTH;
        const uint8_t ground =
            sky->pixels[(uint32_t)(sky->height - 1) * sky->width];
        writeRuns(offset, &end, &ground, 1, cost);
        return 0;
    }

    const unsigned width = sky->width;
    unsigned x = skyX & (width - 1);
    const int shift = (int)(x & 1);
    x -= (unsigned)shift; /* copie par mots, depuis une colonne paire */

    const uint8_t *row = sky->pixels + (uint32_t)y * width;
    unsigned words = SCREEN_WIDTH / 2 + (unsigned)shift;
    uint32_t o = offset;
    while (words > 0) { /* deux morceaux au plus : l'image boucle */
        unsigned n = (width - x) >> 1;
        if (n > words)
            n = words;
        const uint16_t *src = (const uint16_t *)(row + x);
        for (unsigned i = 0; i < n; i++, o += 2)
            vdp_dram_write16(o, BE16(src[i]));
        COST_ADD(cost, skyChunks, 1);
        words -= n;
        x = 0;
    }
    return shift;
}

int render_line(uint32_t offset, const RoadLine *line, const Bitmap *sky,
                unsigned skyX, int y, RenderCost *cost) {
    if (line->type == LINE_ROAD) {
        renderRoad(offset, line, cost);
        return 0;
    }
    return renderSky(offset, sky, skyX, y, cost);
}
