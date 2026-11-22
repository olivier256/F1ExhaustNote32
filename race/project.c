#include "project.h"

#include "fixed.h"

/* =========================================================================
   Projection du circuit (voir project.h).

   Le début de chaque segment devant la caméra est projeté une fois : il
   sert de bord lointain au segment qui le précède et de bord proche au
   suivant. Coût : une division par point projeté (l'échelle FOCAL / z),
   une par segment dessiné (l'inverse de sa hauteur à l'écran).

   Virages : le glissement latéral de la route est la somme des courbures
   des segments traversés, sommée deux fois (une courbe constante donne
   une parabole). Il est nul sous la caméra, et commence par la fraction
   du segment courant qui reste devant elle (curveDX initial), pour que la
   route ne saute pas d'un segment à l'autre.
   ========================================================================= */

typedef struct {
    int32_t x, y, w; /* centre, hauteur et demi-largeur à l'écran, 16.16 */
    int32_t scale;   /* pixels par unité du monde, 16.16 */
} Projected;

/* Ce que se passent les deux étapes : un état de la passe sur l'écran */
typedef struct {
    RoadLine *lines;
    int firstLine, lineStep;
    int clip; /* première ligne déjà couverte : celles du dessous le sont */
} Cover;

/* Pente d'une ligne à l'autre : au plus 64 pixels par ligne (en pratique,
   moins de 10). Assez petite pour que 224 lignes ajoutées à une valeur
   de clamp30 tiennent sur 32 bits. */
#define SLOPE_LIMIT (1 << 22)
static inline int32_t clampSlope(int64_t v) {
    if (v > SLOPE_LIMIT)
        return SLOPE_LIMIT;
    if (v < -SLOPE_LIMIT)
        return -SLOPE_LIMIT;
    return (int32_t)v;
}

/* Décrit les lignes du segment entre 'near' (son début, en bas) et 'far'
   (sa fin, en haut) qui ne sont pas déjà couvertes. */
static void coverSegment(Cover *c, const Projected *near, const Projected *far,
                         uint8_t flags, ProjectCost *cost) {
    /* Bord lointain plus bas que le bord proche : le segment tourne le dos
       à la caméra (descente au-delà d'une crête), on n'en voit rien. */
    if (far->y >= near->y)
        return;

    int top = ceil16(far->y);
    int bottom = ceil16(near->y);
    if (bottom > c->clip)
        bottom = c->clip;
    if (top < 0)
        top = 0;
    if (top >= bottom)
        return; /* caché, ou entre deux lignes */

    /* Variation du centre et de la largeur d'une ligne à la suivante :
       (proche - lointain) / dy, avec inv = 2^38 / dy (dy en 16.16 > 0). */
    const int32_t dy = near->y - far->y;
    const int32_t dy8 = dy >> 8;
    const int32_t inv = (int32_t)div_u32(1u << 30, dy8 > 0 ? (uint32_t)dy8 : 1);
    const int32_t slopeX = clampSlope(mul64(near->x - far->x, inv) >> 22);
    const int32_t slopeW = clampSlope(mul64(near->w - far->w, inv) >> 22);

    /* Valeurs à la ligne 'top', puis à la première ligne du CPU. Elles se
       déduisent toutes de celles de 'top' par additions exactes : un CPU
       qui ne décrit qu'une ligne sur deux trouve les mêmes valeurs qu'une
       description de tout l'écran. */
    const int32_t offset = (top << 16) - far->y; /* >= 0 */
    int32_t x = far->x + clamp30(mul64(slopeX, offset) >> 16);
    int32_t w = far->w + clamp30(mul64(slopeW, offset) >> 16);
    int y = top;
    if (c->lineStep == 2 && ((y ^ c->firstLine) & 1)) {
        y++;
        x += slopeX;
        w += slopeW;
    }
    const int32_t stepX = slopeX * c->lineStep;
    const int32_t stepW = slopeW * c->lineStep;

    for (; y < bottom; y += c->lineStep) {
        RoadLine *l = &c->lines[y];
        l->x = x;
        l->w = w;
        l->type = LINE_ROAD;
        l->flags = flags;
        x += stepX;
        w += stepW;
    }
    COST_ADD(cost, segments, 1);

    c->clip = top;
}

void project_lines(const Track *track, const View *view, RoadLine *lines,
                   int height, int firstLine, int lineStep, SegmentView *views,
                   ProjectCost *cost) {
    const Segment *seg = track->segments;
    const uint32_t length = track_length(track);
    const uint32_t position = view->position;
    const int32_t frac = (int32_t)(position & (SEGMENT_LENGTH - 1));
    unsigned s = position >> SEGMENT_SHIFT;

    /* La caméra suit la route sous la voiture, à CAMERA_HEIGHT au-dessus */
    uint32_t playerZ = position + PLAYER_Z;
    if (playerZ >= length)
        playerZ -= length;
    const int32_t cameraY = track_height(track, playerZ) + CAMERA_HEIGHT;
    const int32_t cameraX = (int32_t)view->playerX * 256; /* 1/256 d'unité */

    Cover c = {lines, firstLine, lineStep, height};
    for (int y = firstLine; y < height; y += lineStep)
        lines[y].type = LINE_SKY;

    int32_t curveX = 0; /* glissement latéral, 1/256 d'unité */
    int32_t curveDX = -((seg[s].curve * frac) >> SEGMENT_SHIFT);

    Projected near = {0, 0, 0, 0};
    int nearVisible = 0;
    if (views)
        for (int n = 0; n < DRAW_DISTANCE; n++)
            views[n].scale = 0;

    for (int n = 0; n < DRAW_DISTANCE && c.clip > 0; n++) {
        const unsigned next = track_next(track, s);

        /* Le début de ce segment, et la route déjà couverte devant lui */
        if (views && nearVisible) {
            views[n].x = near.x;
            views[n].y = near.y;
            views[n].scale = near.scale;
            views[n].clip = (int16_t)c.clip;
        }

        /* Début du segment suivant : à la distance z de la caméra */
        curveX += curveDX;
        curveDX += seg[s].curve;
        const int32_t z = ((n + 1) << SEGMENT_SHIFT) - frac;

        Projected far = {0, 0, 0, 0};
        const int farVisible = z > NEAR_Z;
        if (farVisible) {
            /* Échelle : pixels par unité du monde, 16.16 */
            const int32_t scale =
                (int32_t)div_u32((uint32_t)FOCAL << 16, (uint32_t)z);
            far.x = (SCREEN_CENTER_X << 16) +
                    clamp30(mul64(curveX - cameraX, scale) >> 8);
            far.y = (SCREEN_CENTER_Y << 16) +
                    clamp30(mul64(cameraY - seg[next].y, scale));
            far.w = ROAD_HALF_WIDTH * scale;
            far.scale = scale;
        }

        if (nearVisible && farVisible)
            coverSegment(&c, &near, &far, seg[s].flags, cost);

        near = far;
        nearVisible = farVisible;
        s = next;
    }
}
