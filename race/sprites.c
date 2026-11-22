#include "sprites.h"

#include "fixed.h"
#include "hud.h"
#include "shared.h"
#include "vdp.h"

#include <string.h>

/* ---- Images en SDRAM ------------------------------------------------------- */

#define SDRAM_PIXELS 16384 /* les trois objets de la démo : 13 440 octets */
static uint8_t sdramPixels[SDRAM_PIXELS] __attribute__((aligned(4)));
static Bitmap sdramBitmaps[MAX_PROP_KINDS * PROP_MIPS];
static PropKind sdramKinds[MAX_PROP_KINDS];

const PropKind *sprites_init(const PropKind *kinds, int count) {
    uint32_t used = 0;
    if (count > MAX_PROP_KINDS)
        count = MAX_PROP_KINDS;
    for (int k = 0; k < count; k++) {
        sdramKinds[k] = kinds[k];
        for (int m = 0; m < PROP_MIPS; m++) {
            const Bitmap *rom = kinds[k].mips[m];
            const uint32_t bytes = (uint32_t)rom->width * rom->height;
            Bitmap *b = &sdramBitmaps[k * PROP_MIPS + m];
            *b = *rom;
            if (used + bytes <= SDRAM_PIXELS) { /* sinon : reste en ROM */
                memcpy(sdramPixels + used, rom->pixels, bytes);
                b->pixels = sdramPixels + used;
                used = (used + bytes + 3) & ~3u;
            }
            sdramKinds[k].mips[m] = b;
        }
    }
    return sdramKinds;
}

/* ---- Plan ------------------------------------------------------------------- */

static inline int min(int a, int b) { return a < b ? a : b; }

/* Premier objet du circuit dont le segment est >= segment */
static unsigned firstObjectFrom(const Track *t, unsigned segment) {
    unsigned lo = 0, hi = t->objectCount;
    while (lo < hi) {
        const unsigned mid = (lo + hi) >> 1;
        if (t->objects[mid].segment < segment)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* Taille en cellules d'un carré de l'objet (8 pixels par cellule) */
static inline int cells(int pixels) { return (pixels + 7) >> 3; }

void sprites_plan(const Track *track, const PropKind *kinds,
                  const View *view, const SegmentView *views,
                  SpritePlan *plan) {
    /* Occupation des lignes d'écran par les sprites Mega Drive : le
       compteur d'abord (deux sprites, 40 pixels) */
    uint8_t lineSprites[224];
    uint16_t linePixels[224];
    memset(lineSprites, 0, sizeof lineSprites);
    memset(linePixels, 0, sizeof linePixels);
    for (int y = HUD_Y; y < HUD_Y + 16; y++) {
        lineSprites[y] = HUD_ENTRIES;
        linePixels[y] = 8 * HUD_DIGITS;
    }

    plan->count = 0;
    plan->tiles = HUD_TILES;
    plan->entries = HUD_ENTRIES;
    if (track->objectCount == 0)
        return;

    /* Les objets devant la caméra, du plus proche au plus lointain : à
       partir du segment suivant, en bouclant à la fin du circuit */
    const unsigned base = view->position >> SEGMENT_SHIFT;
    unsigned i = firstObjectFrom(track, base + 1);
    unsigned wrap = 0;
    for (unsigned k = 0; k < track->objectCount && plan->count < MAX_SHOWN;
         k++, i++) {
        if (i == track->objectCount) {
            i = 0;
            wrap = track->count;
        }
        const TrackObject *o = &track->objects[i];
        const unsigned n = o->segment + wrap - base;
        if (n >= DRAW_DISTANCE)
            break;
        const SegmentView *v = &views[n];
        if (n == 0 || v->scale == 0)
            continue;

        /* Rectangle à l'écran : le bas au niveau de la route, centré sur
           l'écart de l'objet */
        const PropKind *kind = &kinds[o->kind];
        const int w = (int)(((uint32_t)kind->worldWidth * (uint32_t)v->scale +
                             0x8000) >> 16);
        const int h = (int)(((uint32_t)kind->worldHeight * (uint32_t)v->scale +
                             0x8000) >> 16);
        if (w < 2 || h < 2 || w > MAX_PROP_WIDTH)
            continue;
        const int32_t center = v->x + (int32_t)o->x * v->scale;
        const int x = ((center + 0x8000) >> 16) - (w >> 1);
        const int y = ((v->y + 0x8000) >> 16) - h;
        const int visibleH = min(h, v->clip - y); /* crête */
        if (visibleH <= 0 || !sprites_chunk_shown(x, y, w, visibleH))
            continue;

        /* Ses sprites (carrés de CHUNK pixels qui touchent l'écran) :
           tiles, entrées, et place sur les lignes. Deux passes : la
           première compte et vérifie que tout tient, la seconde réserve
           les lignes. */
        int tiles = 0, entries = 0, fits = 1;
        for (int pass = 0; pass < 2 && fits; pass++)
            for (int cy = 0; cy < visibleH; cy += CHUNK)
                for (int cx = 0; cx < w; cx += CHUNK) {
                    const int cw = min(CHUNK, w - cx);
                    const int ch = min(CHUNK, visibleH - cy);
                    if (!sprites_chunk_shown(x + cx, y + cy, cw, ch))
                        continue;
                    const int top = y + cy < 0 ? 0 : y + cy;
                    const int bottom = min(y + cy + cells(ch) * 8, 224);
                    if (pass == 0) {
                        tiles += cells(cw) * cells(ch);
                        entries++;
                        for (int r = top; r < bottom; r++)
                            if (lineSprites[r] + 1 > 20 ||
                                linePixels[r] + cells(cw) * 8 > 320)
                                fits = 0;
                    } else {
                        for (int r = top; r < bottom; r++) {
                            lineSprites[r]++;
                            linePixels[r] = (uint16_t)(linePixels[r] +
                                                       cells(cw) * 8);
                        }
                    }
                    if (pass == 0 && (plan->tiles + tiles > MD_STAGE_MAX_TILES ||
                                      plan->entries + entries >
                                          MD_STAGE_MAX_ENTRIES))
                        fits = 0;
                }
        if (!fits)
            continue;

        /* La plus petite taille source au moins aussi large */
        int mip = PROP_MIPS - 1;
        while (mip > 0 && kind->mips[mip]->width < w)
            mip--;
        const Bitmap *src = kind->mips[mip];

        PlacedObject *p = &plan->objects[plan->count++];
        p->x = (int16_t)x;
        p->y = (int16_t)y;
        p->w = (int16_t)w;
        p->h = (int16_t)h;
        p->visibleH = (int16_t)visibleH;
        p->kind = o->kind;
        p->mip = (uint8_t)mip;
        p->stepX = div_u32((uint32_t)src->width << 16, (uint32_t)w);
        p->stepY = div_u32((uint32_t)src->height << 16, (uint32_t)h);
        p->firstTile = plan->tiles;
        p->firstEntry = plan->entries;
        p->entries = (uint8_t)entries;
        plan->tiles = (uint16_t)(plan->tiles + tiles);
        plan->entries = (uint8_t)(plan->entries + entries);
    }
}

/* ---- Mise à l'échelle -------------------------------------------------------- */

/* Colonne source de chaque colonne de l'objet, une table par CPU */
static uint16_t sourceColumn[2][MAX_PROP_WIDTH];

static inline uint32_t tileOffset(unsigned tile) {
    return MD_STAGE_OFFSET + MD_STAGE_HEADER + tile * 32u;
}

static inline uint32_t entryOffset(const SpritePlan *plan, unsigned entry) {
    return tileOffset(plan->tiles) + entry * 8u;
}

void sprites_draw(const SpritePlan *plan, const PropKind *kinds, int cpu,
                  SpriteCost *cost) {
    uint16_t *column = sourceColumn[cpu & 1];

    for (int i = cpu; i < plan->count; i += 2) {
        const PlacedObject *o = &plan->objects[i];
        const Bitmap *src = kinds[o->kind].mips[o->mip];
        for (int ox = 0; ox < o->w; ox++)
            column[ox] = (uint16_t)(((uint32_t)ox * o->stepX) >> 16);

        unsigned tile = o->firstTile, entry = o->firstEntry;
        for (int cy = 0; cy < o->visibleH; cy += CHUNK)
            for (int cx = 0; cx < o->w; cx += CHUNK) {
                const int cw = min(CHUNK, o->w - cx);
                const int ch = min(CHUNK, o->visibleH - cy);
                if (!sprites_chunk_shown(o->x + cx, o->y + cy, cw, ch))
                    continue;
                const int cellsW = cells(cw), cellsH = cells(ch);

                /* Tiles du sprite, colonne par colonne ; une rangée de tile
                   = 8 pixels de 4 bits = deux mots */
                const unsigned first = tile;
                for (int c = 0; c < cellsW; c++)
                    for (int r = 0; r < cellsH; r++) {
                        uint32_t out = tileOffset(tile++);
                        for (int py = 0; py < 8; py++, out += 4) {
                            const int oy = cy + 8 * r + py;
                            const uint8_t *row =
                                oy < o->visibleH
                                    ? src->pixels +
                                          (((uint32_t)oy * o->stepY) >> 16) *
                                              src->width
                                    : 0;
                            uint32_t pixels = 0;
                            for (int px = 0; px < 8; px++) {
                                const int ox = cx + 8 * c + px;
                                const unsigned p =
                                    (row && ox < o->w) ? row[column[ox]] : 0;
                                pixels = pixels << 4 | (p & 15);
                            }
                            vdp_dram_write16(out, (uint16_t)(pixels >> 16));
                            vdp_dram_write16(out + 2, (uint16_t)pixels);
                        }
                        COST_ADD(cost, tiles, 1);
                    }

                /* Son entrée : palette 1, priorité, chaînée à la suivante */
                const uint32_t e = entryOffset(plan, entry);
                const unsigned link = entry + 1 == plan->entries
                                          ? 0
                                          : MD_RESERVED_SPRITES + entry + 1;
                vdp_dram_write16(e, (uint16_t)(o->y + cy + 128));
                vdp_dram_write16(e + 2, (uint16_t)((cellsW - 1) << 10 |
                                                   (cellsH - 1) << 8 | link));
                vdp_dram_write16(e + 4, (uint16_t)(0x8000 | 0x2000 |
                                                   (MD_SH2_FIRST_TILE + first)));
                vdp_dram_write16(e + 6, (uint16_t)(o->x + cx + 128));
                entry++;
            }
        COST_ADD(cost, objects, 1);
    }
}

void sprites_write_header(const SpritePlan *plan) {
    vdp_dram_write16(MD_STAGE_OFFSET, (uint16_t)(plan->tiles * 32u));
    vdp_dram_write16(MD_STAGE_OFFSET + 2, plan->entries);
}
