#include "compose.h"
#include "framebuffer.h"
#include "vdp.h"

/* =========================================================================
   Composition d'une ligne par segments.

   Principe
   --------
   On parcourt la ligne du plan A tile par tile. bmp2c a donné la classe
   de chaque ligne de tile (assets_types.h) :
   - transparente : l'écran montre le plan B, qu'on copie ;
   - opaque       : l'écran montre le plan A, qu'on copie ; B n'est pas lu ;
   - mixte        : on fusionne pixel par pixel (A si non nul, sinon B).
   Les tiles voisines de même classe forment un segment. Sur l'image de la
   démo, 95 % des lignes de tile de A sont entièrement transparentes ou
   entièrement opaques : la fusion, plus chère, reste rare.

   Règles d'écriture (comme dans AMB96)
   ------------------------------------
   Dans le frame buffer, uniquement des écritures 16 bits à des adresses
   paires : on écrit des mots d'écran (pixels 2k et 2k + 1). La ligne
   commence à une adresse paire et l'écran est écrit à partir de x = 0 :
   la destination est toujours paire. C'est la source qui hérite de la
   parité du défilement :
   - source paire : chaque mot est lu d'un coup (MOV.W) dans la tile ;
   - source impaire : deux pixels lus séparément (deux MOV.B, depuis le
     cache), assemblés en un mot.
   Tiles et plans ont une largeur paire : la parité de la source ne change
   pas le long de la ligne. La décision est prise une fois par ligne et par
   plan (copyA, copyB), pas pour chaque segment.

   Frontières de segment
   ---------------------
   Si le défilement de A est impair, les frontières de tile de A tombent à
   des colonnes d'écran impaires : le mot qui chevauche deux segments passe
   par la fusion (un mot). C'est juste dans tous les cas ; on pourra plus
   tard reconnaître opaque + opaque ou transparent + transparent.
   ========================================================================= */

/* Ramène v dans [0, n) */
static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

/* x + d, ramené dans [0, n), pour 0 <= x < n et d >= 0 */
static int advance(int x, int d, int n) {
    x += d;
    while (x >= n)
        x -= n;
    return x;
}

/* Deux pixels, l'un derrière l'autre, lus à une adresse paire : un MOV.W
   sur SH2. Sur l'hôte (petit-boutiste), on rétablit l'ordre du SH2 :
   pixel de gauche dans l'octet de poids fort. */
static inline uint16_t readPair(const uint8_t *p) {
#if defined(__sh__)
    return *(const uint16_t *)p;
#else
    return (uint16_t)((p[0] << 8) | p[1]);
#endif
}

/* Assemble deux pixels lus séparément (deux MOV.B) en un mot */
static inline uint16_t makePair(uint8_t left, uint8_t right) {
    return (uint16_t)((left << 8) | right);
}

/* ---- Une ligne d'un plan --------------------------------------------------
 */

typedef struct {
    const uint16_t *row;     /* numéros de tile de la rangée traversée */
    const uint8_t *tileLine; /* ligne voulue dans la tile 0 */
    int rowInTile;           /* numéro de cette ligne dans la tile */
    int width;               /* largeur du plan en pixels */
} PlaneRow;

static void planeRowInit(PlaneRow *pr, const Tilemap *map, const Tileset *tiles,
                         int worldY) {
    const int y = wrap(worldY, map->heightTiles << TILE_SHIFT);
    pr->row = map->cells + (y >> TILE_SHIFT) * map->widthTiles;
    pr->rowInTile = y & (TILE_SIZE - 1);
    pr->tileLine = tiles->pixels + pr->rowInTile * TILE_SIZE;
    pr->width = map->widthTiles << TILE_SHIFT;
}

/* Premier pixel du morceau qui commence à la colonne x du plan, et
   nombre de pixels jusqu'à la fin de la tile (au plus 'remaining') */
static inline const uint8_t *chunkAt(const PlaneRow *pr, int x, int remaining,
                                     int *count) {
    const int xInTile = x & (TILE_SIZE - 1);
    const int n = TILE_SIZE - xInTile;
    *count = n < remaining ? n : remaining;
    return pr->tileLine + pr->row[x >> TILE_SHIFT] * TILE_PIXELS + xInTile;
}

/* ---- Copie d'un segment depuis un plan ------------------------------------
   n pixels (pair) du plan, à partir de sa colonne x, vers l'offset 'offset'
   (pair) du frame buffer. */

typedef void (*CopySpan)(uint32_t offset, const PlaneRow *pr, int x, int n,
                         ComposeCost *cost);

/* Source paire : chaque morceau commence à une adresse paire et a une
   longueur paire, dans la tile comme dans le frame buffer. */
static void copySpanEven(uint32_t offset, const PlaneRow *pr, int x, int n,
                         ComposeCost *cost) {
    COST_ADD(cost, wordsEven, n / 2);
    while (n > 0) {
        int count;
        const uint8_t *src = chunkAt(pr, x, n, &count);
        COST_ADD(cost, tileChunks, 1);

        for (int i = 0; i < count; i += 2) {
            vdp_dram_write16(offset, readPair(src + i));
            offset += 2;
        }

        n -= count;
        x = advance(x, count, pr->width);
    }
}

/* Source impaire : chaque mot écrit réunit un pixel de colonne impaire et
   le suivant. Le dernier pixel d'une tile (colonne 15) va donc avec le
   premier de la tile suivante : il est gardé de côté ('carry'). */
static void copySpanOdd(uint32_t offset, const PlaneRow *pr, int x, int n,
                        ComposeCost *cost) {
    uint8_t carry = 0;
    int hasCarry = 0;

    COST_ADD(cost, wordsOdd, n / 2);
    while (n > 0) {
        int count;
        const uint8_t *src = chunkAt(pr, x, n, &count);
        int i = 0;
        COST_ADD(cost, tileChunks, 1);

        if (hasCarry) {
            vdp_dram_write16(offset, makePair(carry, src[0]));
            offset += 2;
            i = 1;
            hasCarry = 0;
        }

        for (; i + 1 < count; i += 2) {
            vdp_dram_write16(offset, makePair(src[i], src[i + 1]));
            offset += 2;
        }

        if (i < count) {
            carry = src[i];
            hasCarry = 1;
        }

        n -= count;
        x = advance(x, count, pr->width);
    }
    /* n étant pair, il ne reste jamais de pixel en attente ici */
}

/* ---- Fusion pixel par pixel ------------------------------------------------
   Lecture séquentielle d'un plan, un pixel à la fois. */

typedef struct {
    const PlaneRow *pr;
    const uint8_t *src; /* prochain pixel */
    int left;           /* pixels restants dans le morceau de tile */
    int x;              /* colonne du plan du prochain morceau */
} Cursor;

static inline void cursorInit(Cursor *c, const PlaneRow *pr, int x) {
    c->pr = pr;
    c->src = 0;
    c->left = 0;
    c->x = x;
}

static inline uint8_t cursorNext(Cursor *c, ComposeCost *cost) {
    if (c->left == 0) {
        c->src = chunkAt(c->pr, c->x, TILE_SIZE, &c->left);
        c->x = advance(c->x, c->left, c->pr->width);
        COST_ADD(cost, tileChunks, 1);
    }
    c->left--;
    return *c->src++;
}

/* n pixels (pair) : A s'il est non nul, sinon B. Les deux plans sont lus
   à chaque pixel : ce chemin ne sert qu'aux lignes de tile mixtes et aux
   frontières de segment, rares. */
static void mergeSpan(uint32_t offset, const PlaneRow *pa, int xa,
                      const PlaneRow *pb, int xb, int n, ComposeCost *cost) {
    Cursor a, b;
    cursorInit(&a, pa, xa);
    cursorInit(&b, pb, xb);

    COST_ADD(cost, wordsMerged, n / 2);
    for (int i = 0; i < n; i += 2) {
        uint8_t left = cursorNext(&a, cost);
        uint8_t backLeft = cursorNext(&b, cost);
        uint8_t right = cursorNext(&a, cost);
        uint8_t backRight = cursorNext(&b, cost);
        if (left == 0)
            left = backLeft;
        if (right == 0)
            right = backRight;
        vdp_dram_write16(offset, makePair(left, right));
        offset += 2;
    }
}

/* ---- La ligne -------------------------------------------------------------
 */

/* Segment de la ligne d'écran : colonnes [start, end), une classe */
typedef struct {
    int16_t start, end;
    int16_t rowClass;
} Run;

/* Une tile de A par segment au pire, plus les deux tiles entamées */
#define MAX_RUNS (FB_WIDTH / TILE_SIZE + 2)

void compose_line(uint32_t offset, const Tileset *tiles, const Tilemap *planeA,
                  int scrollAX, int worldAY, const Tilemap *planeB,
                  int scrollBX, int worldBY, ComposeCost *cost) {
    PlaneRow pa, pb;
    planeRowInit(&pa, planeA, tiles, worldAY);
    planeRowInit(&pb, planeB, tiles, worldBY);

    const int xa0 = wrap(scrollAX, pa.width);
    const int xb0 = wrap(scrollBX, pb.width);

    /* Une décision de parité par plan, pour toute la ligne */
    const CopySpan copyA = (xa0 & 1) ? copySpanOdd : copySpanEven;
    const CopySpan copyB = (xb0 & 1) ? copySpanOdd : copySpanEven;

    COST_ADD(cost, lines, 1);

    /* 1. Segments : la ligne de A, tile par tile, les tiles voisines de
          même classe regroupées. */
    Run runs[MAX_RUNS];
    int runCount = 0;
    for (int sx = 0, x = xa0; sx < FB_WIDTH;) {
        int n = TILE_SIZE - (x & (TILE_SIZE - 1));
        if (n > FB_WIDTH - sx)
            n = FB_WIDTH - sx;
        const int rowClass =
            tile_row_class(tiles, pa.row[x >> TILE_SHIFT], pa.rowInTile);
        COST_ADD(cost, aTiles, 1);

        if (runCount > 0 && runs[runCount - 1].rowClass == rowClass) {
            runs[runCount - 1].end = (int16_t)(sx + n);
        } else {
            runs[runCount].start = (int16_t)sx;
            runs[runCount].end = (int16_t)(sx + n);
            runs[runCount].rowClass = (int16_t)rowClass;
            runCount++;
        }

        sx += n;
        x = advance(x, n, pa.width);
    }

    /* 2. Dessin, mot par mot. 'p' est la prochaine colonne d'écran à
          écrire, toujours paire. Un segment qui commence à une colonne
          impaire a déjà eu son premier pixel écrit avec le mot de
          frontière précédent. */
    int p = 0;
    for (int r = 0; r < runCount; r++) {
        const Run *run = &runs[r];
        const int end = run->end & ~1; /* fin des mots entiers du segment */

        if (end > p) {
            const int n = end - p;
            const int xa = advance(xa0, p, pa.width);
            const int xb = advance(xb0, p, pb.width);
            COST_ADD(cost, runs, 1);

            switch (run->rowClass) {
            case ROW_OPAQUE:
                copyA(offset + (uint32_t)p, &pa, xa, n, cost);
                break;
            case ROW_TRANSPARENT:
                copyB(offset + (uint32_t)p, &pb, xb, n, cost);
                break;
            default:
                mergeSpan(offset + (uint32_t)p, &pa, xa, &pb, xb, n, cost);
                break;
            }
            p = end;
        }

        /* Frontière à une colonne impaire : le mot (end, end + 1) est à
           cheval sur ce segment et le suivant. */
        if (run->end & 1) {
            COST_ADD(cost, runs, 1);
            mergeSpan(offset + (uint32_t)p, &pa, advance(xa0, p, pa.width), &pb,
                      advance(xb0, p, pb.width), 2, cost);
            p += 2;
        }
    }
}
