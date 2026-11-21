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
   par la fusion (un mot).

   Écrit pour le SH2 (mesuré avec tools/sh2cycles)
   -----------------------------------------------
   Le SH2 n'a ni division, ni décalage à distance variable (seulement 1,
   2, 8 ou 16 bits) : un « % » ou un « >> n » devient un appel à libgcc.
   Le code les évite :
   - positions ramenées dans le plan par soustraction (wrapIn) ;
   - classe d'une ligne de tile lue avec des masques préparés une fois par
     ligne (classMasks), sans décalage ;
   - positions en non signé : x >> 4 devient deux SHLR2 ;
   - tiles parcourues par un pointeur sur leur numéro dans le plan, qui
     revient au début de la rangée au bout du plan ;
   - tiles entières copiées par une suite de 8 mots, sans boucle.
   ========================================================================= */

/* Fonctions appelées quelques fois par ligne : gardées en un seul
   exemplaire (pas de recopie dans compose_line), pour que le code de
   rendu tienne dans les 2 Ko de RAM interne du SH2. */
#define ONCE_PER_LINE __attribute__((noinline))

/* Code froid : la copie depuis une source impaire. Avec le décalage
   d'écran choisi ligne par ligne, elle ne sert plus qu'au plan minoritaire
   d'une ligne, quelques pour cent des mots.
   Sur SH2, il est rangé à part (section .text.sdram) : l'édition de liens
   le laisse en SDRAM, derrière le cache, et réserve la RAM interne au
   code chaud (voir tools/sh2cycles/bench_ram.ld). */
#if defined(__sh__)
#define COLD_CODE __attribute__((noinline, section(".text.sdram")))
#else
#define COLD_CODE __attribute__((noinline))
#endif

/* Ramène v dans [0, n), par additions ou soustractions : les valeurs
   courantes (un défilement, une ligne) sont à moins d'un tour du plan. */
static unsigned wrapIn(int v, unsigned n) {
    while (v < 0)
        v += (int)n;
    while ((unsigned)v >= n)
        v -= (int)n;
    return (unsigned)v;
}

/* x + d, ramené dans [0, n), pour x < n */
static unsigned advance(unsigned x, unsigned d, unsigned n) {
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

/* ---- Destination dans le frame buffer --------------------------------------
   Sur SH2 : un pointeur. Les mots d'une tile s'écrivent alors en
   MOV.W R0,@(disp,Rn), sans recalculer l'adresse à chaque mot.
   Sur l'hôte : un offset, et chaque écriture passe par vdp_dram_write16(),
   qui la compte. */
#if defined(__sh__)
typedef volatile uint16_t *Dest;
#define DEST(offset) ((Dest)(VDP_DRAM_ADDRESS + (offset)))
#define PUT(d, k, v) ((d)[k] = (v)) /* mot k après d */
#define SKIP(d, words) ((d) += (words))
#else
typedef uint32_t Dest;
#define DEST(offset) (offset)
#define PUT(d, k, v) vdp_dram_write16((d) + 2 * (k), (v))
#define SKIP(d, words) ((d) += 2 * (words))
#endif

/* Quatre pixels lus à une adresse multiple de 4 : un MOV.L sur SH2 */
static inline uint32_t readQuad(const uint8_t *p) {
#if defined(__sh__)
    return *(const uint32_t *)p;
#else
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
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
    const uint16_t *rowEnd;  /* juste après le dernier */
    const uint8_t *tileLine; /* ligne voulue dans la tile 0 */
    unsigned rowInTile;      /* numéro de cette ligne dans la tile */
    unsigned width;          /* largeur du plan en pixels */
} PlaneRow;

static void planeRowInit(PlaneRow *pr, const Tilemap *map, const Tileset *tiles,
                         int worldY) {
    const unsigned y = wrapIn(worldY, (unsigned)map->heightTiles << TILE_SHIFT);
    pr->row = map->cells + (y >> TILE_SHIFT) * map->widthTiles;
    pr->rowEnd = pr->row + map->widthTiles;
    pr->rowInTile = y & (TILE_SIZE - 1);
    pr->tileLine = tile_row(tiles, 0, pr->rowInTile);
    pr->width = (unsigned)map->widthTiles << TILE_SHIFT;
}

/* Premier pixel de la tile dont 'cell' donne le numéro, sur la ligne */
static inline const uint8_t *tileStart(const PlaneRow *pr,
                                       const uint16_t *cell) {
    return pr->tileLine + (unsigned)*cell * TILE_PIXELS;
}

/* Tile suivante dans la rangée (le plan boucle) */
static inline const uint16_t *nextCell(const PlaneRow *pr,
                                       const uint16_t *cell) {
    return ++cell == pr->rowEnd ? pr->row : cell;
}

/* ---- Copie d'un segment depuis un plan ------------------------------------
   n pixels (pair) du plan, à partir de sa colonne x, vers l'offset 'offset'
   (pair) du frame buffer. */

typedef void (*CopySpan)(Dest d, const PlaneRow *pr, unsigned x, unsigned n,
                         ComposeCost *cost);

/* Une ligne de tile entière, source paire : 8 mots, sans boucle */
static inline void copyTileEven(Dest d, const uint8_t *src) {
    PUT(d, 0, readPair(src));
    PUT(d, 1, readPair(src + 2));
    PUT(d, 2, readPair(src + 4));
    PUT(d, 3, readPair(src + 6));
    PUT(d, 4, readPair(src + 8));
    PUT(d, 5, readPair(src + 10));
    PUT(d, 6, readPair(src + 12));
    PUT(d, 7, readPair(src + 14));
}

/* Source paire : chaque morceau commence à une adresse paire et a une
   longueur paire, dans la tile comme dans le frame buffer. */
static void copySpanEven(Dest d, const PlaneRow *pr, unsigned x, unsigned n,
                         ComposeCost *cost) {
    const uint16_t *cell = pr->row + (x >> TILE_SHIFT);
    unsigned xInTile = x & (TILE_SIZE - 1);

    COST_ADD(cost, wordsEven, n / 2);
    while (n > 0) {
        const uint8_t *src = tileStart(pr, cell) + xInTile;
        cell = nextCell(pr, cell);
        COST_ADD(cost, tileChunks, 1);

        if (xInTile == 0 && n >= TILE_SIZE) {
            copyTileEven(d, src);
            SKIP(d, TILE_SIZE / 2);
            n -= TILE_SIZE;
            continue;
        }

        unsigned count = TILE_SIZE - xInTile;
        if (count > n)
            count = n;
        n -= count;
        xInTile = 0;
        for (; count > 0; count -= 2) {
            PUT(d, 0, readPair(src));
            SKIP(d, 1);
            src += 2;
        }
    }
}

/* Une ligne de tile entière, source impaire : le pixel gardé de la tile
   précédente avec le pixel 0, puis (1, 2) ... (13, 14). Renvoie le
   pixel 15, qui ira avec le premier de la tile suivante. */
static inline uint8_t copyTileOdd(Dest d, const uint8_t *src, uint8_t carry) {
    /* Les 16 pixels en quatre mots longs (MOV.L, gros-boutiste) : le mot
       écrit garde les 16 bits de poids faible, sans masque. */
    const uint32_t l0 = readQuad(src), l1 = readQuad(src + 4),
                   l2 = readQuad(src + 8), l3 = readQuad(src + 12);
    PUT(d, 0, (uint16_t)((carry << 8) | (l0 >> 24)));
    PUT(d, 1, (uint16_t)(l0 >> 8));
    PUT(d, 2, (uint16_t)((l0 << 8) | (l1 >> 24)));
    PUT(d, 3, (uint16_t)(l1 >> 8));
    PUT(d, 4, (uint16_t)((l1 << 8) | (l2 >> 24)));
    PUT(d, 5, (uint16_t)(l2 >> 8));
    PUT(d, 6, (uint16_t)((l2 << 8) | (l3 >> 24)));
    PUT(d, 7, (uint16_t)(l3 >> 8));
    return (uint8_t)l3;
}

/* Source impaire : chaque mot écrit réunit un pixel de colonne impaire et
   le suivant. Le dernier pixel d'une tile (colonne 15) va donc avec le
   premier de la tile suivante : il est gardé de côté ('carry'). */
COLD_CODE static void copySpanOdd(Dest d, const PlaneRow *pr, unsigned x,
                                  unsigned n, ComposeCost *cost) {
    const uint16_t *cell = pr->row + (x >> TILE_SHIFT);
    unsigned xInTile = x & (TILE_SIZE - 1);
    uint8_t carry = 0;
    int hasCarry = 0;

    COST_ADD(cost, wordsOdd, n / 2);
    while (n > 0) {
        const uint8_t *src = tileStart(pr, cell) + xInTile;
        cell = nextCell(pr, cell);
        COST_ADD(cost, tileChunks, 1);

        if (hasCarry && xInTile == 0 && n >= TILE_SIZE) {
            carry = copyTileOdd(d, src, carry);
            SKIP(d, TILE_SIZE / 2);
            n -= TILE_SIZE;
            continue;
        }

        unsigned count = TILE_SIZE - xInTile;
        if (count > n)
            count = n;
        n -= count;
        xInTile = 0;

        unsigned i = 0;
        if (hasCarry) {
            PUT(d, 0, makePair(carry, src[0]));
            SKIP(d, 1);
            i = 1;
            hasCarry = 0;
        }
        for (; i + 1 < count; i += 2) {
            PUT(d, 0, makePair(src[i], src[i + 1]));
            SKIP(d, 1);
        }
        if (i < count) {
            carry = src[i];
            hasCarry = 1;
        }
    }
    /* n étant pair, il ne reste jamais de pixel en attente ici */
}

/* ---- Fusion pixel par pixel ------------------------------------------------
   n pixels (pair) : A s'il est non nul, sinon B. Ce chemin ne sert
   qu'aux lignes de tile mixtes et aux frontières de segment, rares.
   On avance par morceaux où ni A ni B ne change de tile : à l'intérieur,
   deux pointeurs, sans test de fin de tile à chaque pixel. Les pixels
   sont réunis deux par deux en mots ; un morceau peut finir sur un pixel
   de gauche, qui attend alors le pixel suivant ('pending'). */

typedef struct {
    const uint16_t *cell; /* tile suivante */
    const uint8_t *src;   /* prochain pixel */
    unsigned left;        /* pixels restants dans la tile en cours */
} Cursor;

static inline void cursorAt(Cursor *c, const PlaneRow *pr, unsigned x) {
    const uint16_t *cell = pr->row + (x >> TILE_SHIFT);
    const unsigned xInTile = x & (TILE_SIZE - 1);
    c->src = tileStart(pr, cell) + xInTile;
    c->left = TILE_SIZE - xInTile;
    c->cell = nextCell(pr, cell);
}

static inline void cursorNextTile(Cursor *c, const PlaneRow *pr) {
    c->src = tileStart(pr, c->cell);
    c->cell = nextCell(pr, c->cell);
    c->left = TILE_SIZE;
}

ONCE_PER_LINE static void mergeSpan(Dest d, const PlaneRow *pa, unsigned xa,
                                    const PlaneRow *pb, unsigned xb, unsigned n,
                                    ComposeCost *cost) {
    Cursor a, b;
    cursorAt(&a, pa, xa);
    cursorAt(&b, pb, xb);
    COST_ADD(cost, tileChunks, 2);
    COST_ADD(cost, wordsMerged, n / 2);

    unsigned pending = 0, hasPending = 0;
    for (;;) {
        unsigned piece = a.left < b.left ? a.left : b.left;
        if (piece > n)
            piece = n;
        n -= piece;
        a.left -= piece;
        b.left -= piece;

        const uint8_t *srcA = a.src, *srcB = b.src;
        a.src += piece;
        b.src += piece;

        /* Sans pixel en attente : deux pixels à la fois, un mot écrit */
        if (!hasPending) {
            for (; piece >= 2; piece -= 2) {
                unsigned left = srcA[0], right = srcA[1];
                if (left == 0)
                    left = srcB[0];
                if (right == 0)
                    right = srcB[1];
                PUT(d, 0, (uint16_t)((left << 8) | right));
                SKIP(d, 1);
                srcA += 2;
                srcB += 2;
            }
        }
        for (; piece > 0; piece--) {
            unsigned pixel = *srcA++;
            const unsigned back = *srcB++;
            if (pixel == 0)
                pixel = back;
            if (hasPending) {
                PUT(d, 0, (uint16_t)((pending << 8) | pixel));
                SKIP(d, 1);
                hasPending = 0;
            } else {
                pending = pixel;
                hasPending = 1;
            }
        }

        if (n == 0)
            break;
        if (a.left == 0) {
            cursorNextTile(&a, pa);
            COST_ADD(cost, tileChunks, 1);
        }
        if (b.left == 0) {
            cursorNextTile(&b, pb);
            COST_ADD(cost, tileChunks, 1);
        }
    }
    /* n étant pair, il ne reste jamais de pixel en attente ici */
}

/* Un pixel d'un plan, à sa colonne x (x < largeur) */
static inline unsigned pixelAt(const PlaneRow *pr, unsigned x) {
    return tileStart(pr, pr->row + (x >> TILE_SHIFT))[x & (TILE_SIZE - 1)];
}

/* Le mot de frontière entre deux segments (colonnes xa et xa + 1 de A,
   xb et xb + 1 de B) : quatre pixels lus directement, sans curseurs. */
ONCE_PER_LINE static void mergeWord(Dest d, const PlaneRow *pa, unsigned xa,
                                    const PlaneRow *pb, unsigned xb,
                                    ComposeCost *cost) {
    const unsigned xa1 = xa + 1 == pa->width ? 0 : xa + 1;
    const unsigned xb1 = xb + 1 == pb->width ? 0 : xb + 1;
    unsigned left = pixelAt(pa, xa), right = pixelAt(pa, xa1);
    if (left == 0)
        left = pixelAt(pb, xb);
    if (right == 0)
        right = pixelAt(pb, xb1);
    PUT(d, 0, (uint16_t)((left << 8) | right));
    COST_ADD(cost, wordsMerged, 1);
}

/* ---- La ligne -------------------------------------------------------------
 */

/* Masque des 2 bits de classe de chaque ligne de tile (assets_types.h) :
   la classe se lit par un ET, sans décalage à distance variable. */
static const uint32_t classMasks[TILE_SIZE] = {
    0x00000003, 0x0000000C, 0x00000030, 0x000000C0, 0x00000300, 0x00000C00,
    0x00003000, 0x0000C000, 0x00030000, 0x000C0000, 0x00300000, 0x00C00000,
    0x03000000, 0x0C000000, 0x30000000, 0xC0000000,
};
/* Bits de la classe opaque (01) dans toutes les lignes */
#define ALL_ROWS_OPAQUE 0x55555555u

/* Segment de la ligne : colonnes [start, end) comptées depuis la colonne
   scrollAX - 1 du plan A, une classe (déjà masquée : comparée aux valeurs
   de la ligne, sans décalage) */
typedef struct {
    int16_t start, end;
    uint32_t rowClass;
} Run;

/* Segments formés sur LINE_SPAN pixels, de la colonne scrollAX - 1 à la
   colonne scrollAX + 320 de A : de quoi dessiner la ligne avec ou sans
   décalage (voir compose_line). */
#define LINE_SPAN (FB_WIDTH + 2)

/* Une tile de A par segment au pire, plus les deux tiles entamées */
#define MAX_RUNS (LINE_SPAN / TILE_SIZE + 2)

/* Choix du décalage : surcoûts, en cycles SH2, d'un mot copié depuis une
   source impaire et d'un mot de frontière (fusion), par rapport à un mot
   copié depuis une source paire. Ordres de grandeur mesurés avec
   tools/sh2cycles ; seule leur proportion compte. */
#define ODD_WORD_EXTRA 7
#define BOUNDARY_WORD_EXTRA 50

int compose_line(uint32_t offset, const Tileset *tiles, const Tilemap *planeA,
                 int scrollAX, int worldAY, const Tilemap *planeB, int scrollBX,
                 int worldBY, ComposeCost *cost) {
    PlaneRow pa, pb;
    planeRowInit(&pa, planeA, tiles, worldAY);
    planeRowInit(&pb, planeB, tiles, worldBY);

    /* Classes de cette ligne de tile, masquées */
    const uint32_t classMask = classMasks[pa.rowInTile];
    const uint32_t opaque = classMask & ALL_ROWS_OPAQUE;
    const uint32_t *const rowClasses = tiles->rowClasses;

    COST_ADD(cost, lines, 1);

    /* 1. Segments : la ligne de A, tile par tile, depuis la colonne
          scrollAX - 1, les tiles voisines de même classe regroupées. La
          classe du segment en cours reste dans une variable locale (un
          registre) ; le segment n'est rangé qu'une fois terminé. On compte
          au passage les pixels copiés depuis A (opaques) et depuis B
          (transparents), pour le choix du décalage. */
    const unsigned xs = wrapIn(scrollAX - 1, pa.width);
    Run runs[MAX_RUNS];
    Run *run = runs;
    const uint16_t *cell = pa.row + (xs >> TILE_SHIFT);
    unsigned sx = TILE_SIZE - (xs & (TILE_SIZE - 1)); /* tile entamée */
    unsigned start = 0;
    unsigned fromA = 0, fromB = 0;
    uint32_t current = rowClasses[*cell] & classMask;
    cell = nextCell(&pa, cell);
    COST_ADD(cost, aTiles, 1);

    for (; sx < LINE_SPAN; sx += TILE_SIZE) {
        const uint32_t rowClass = rowClasses[*cell] & classMask;
        cell = nextCell(&pa, cell);
        COST_ADD(cost, aTiles, 1);
        if (rowClass != current) {
            if (current == opaque)
                fromA += sx - start;
            else if (current == 0)
                fromB += sx - start;
            run->start = (int16_t)start;
            run->end = (int16_t)sx;
            run->rowClass = current;
            run++;
            start = sx;
            current = rowClass;
        }
    }
    if (current == opaque)
        fromA += LINE_SPAN - start;
    else if (current == 0)
        fromB += LINE_SPAN - start;
    run->start = (int16_t)start;
    run->end = LINE_SPAN; /* la dernière tile est coupée */
    run->rowClass = current;
    const Run *const lastRun = run;

    /* 2. Décalage d'écran (SFT) de cette ligne. Sans décalage, le pixel x
          de l'écran est le pixel x de la ligne, qui montre la colonne
          scrollAX + x de A. Avec décalage, l'écran montre le pixel x + 1
          de la ligne : on la dessine depuis la colonne scrollAX - 1, sur
          322 pixels (161 mots). La parité des deux plans s'inverse : on
          choisit le décalage qui rend pair le plan le plus coûteux en
          source impaire (un A impair coûte aussi ses mots de frontière). */
    const unsigned evenA = !(wrapIn(scrollAX, pa.width) & 1);
    const unsigned evenB = !(wrapIn(scrollBX, pb.width) & 1);
    const unsigned costA = fromA / 2 * ODD_WORD_EXTRA +
                           (unsigned)(lastRun - runs) * BOUNDARY_WORD_EXTRA;
    const unsigned costB = fromB / 2 * ODD_WORD_EXTRA;
    const unsigned costNoShift = (evenA ? 0 : costA) + (evenB ? 0 : costB);
    const unsigned costShift = (evenA ? costA : 0) + (evenB ? costB : 0);
    const int shift = costShift < costNoShift;

    /* Ligne dessinée : 'width' pixels ; son pixel 0 est le pixel 'skip'
       des segments, et montre la colonne xa0 de A et xb0 de B. */
    const unsigned width = FB_WIDTH + 2 * (unsigned)shift;
    const unsigned skip = 1 - (unsigned)shift;
    const unsigned xa0 = advance(xs, skip, pa.width);
    const unsigned xb0 = wrapIn(scrollBX - shift, pb.width);

    /* Une décision de parité par plan, pour toute la ligne */
    const CopySpan copyA = (xa0 & 1) ? copySpanOdd : copySpanEven;
    const CopySpan copyB = (xb0 & 1) ? copySpanOdd : copySpanEven;

    /* 3. Dessin, mot par mot. 'p' est la prochaine colonne de la ligne à
          écrire, toujours paire. Un segment qui commence à une colonne
          impaire a déjà eu son premier pixel écrit avec le mot de
          frontière précédent. */
    unsigned p = 0;
    for (run = runs; run <= lastRun; run++) {
        unsigned runEnd = (unsigned)run->end - skip;
        if (runEnd > width)
            runEnd = width;
        const unsigned end = runEnd & ~1u; /* mots entiers */

        if (end > p) {
            const unsigned count = end - p;
            const unsigned xa = advance(xa0, p, pa.width);
            const unsigned xb = advance(xb0, p, pb.width);
            COST_ADD(cost, runs, 1);

            if (run->rowClass == opaque)
                copyA(DEST(offset + p), &pa, xa, count, cost);
            else if (run->rowClass == 0)
                copyB(DEST(offset + p), &pb, xb, count, cost);
            else
                mergeSpan(DEST(offset + p), &pa, xa, &pb, xb, count, cost);
            p = end;
        }

        /* Frontière à une colonne impaire : le mot (end, end + 1) est à
           cheval sur ce segment et le suivant. */
        if (runEnd & 1) {
            COST_ADD(cost, runs, 1);
            mergeWord(DEST(offset + p), &pa, advance(xa0, p, pa.width), &pb,
                      advance(xb0, p, pb.width), cost);
            p += 2;
        }
    }
    return shift;
}
