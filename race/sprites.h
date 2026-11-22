#ifndef SPRITES_H
#define SPRITES_H

#include <stdint.h>

#include "assets_types.h"
#include "project.h"
#include "world.h"

/* =========================================================================
   Objets du bord de la route, mis à l'échelle par les SH2 en sprites
   Mega Drive.

   Chaque type d'objet existe en PROP_MIPS tailles (pleine, moitié,
   quart : tools/props.py). À chaque image :
   1. Plan (sprites_plan), calculé à l'identique par les deux CPU, sans
      se parler : les objets visibles, du plus proche au plus lointain (au
      plus MAX_SHOWN), leur rectangle à l'écran (taille dans le monde x
      échelle du segment), la ligne de crête qui les coupe, la taille
      source à prendre (la plus petite au moins aussi large que l'objet à
      l'écran), et leur place dans la zone de transfert : tiles et entrées
      de la table des sprites. Un objet est découpé en sprites Mega Drive
      de 32 x 32 pixels au plus (4 x 4 cellules). Il n'est pris que si
      tout tient : budget de tiles et d'entrées (le DMA du VBlank), et 20
      sprites et 320 pixels de sprites par ligne d'écran (sinon le VDP
      Mega Drive en perdrait). Le plus proche passe devant.
   2. Mise à l'échelle (sprites_draw) : chaque CPU fabrique les objets de
      rang pair (Master) ou impair (Slave) du plan. Pour chaque pixel de
      l'objet à l'écran, le pixel source (x, y) = (ox x pasX, oy x pasY),
      pas en 16.16 : des colonnes et des lignes répétées ou omises, comme
      sur Neo Geo. Les lignes sous la crête sont transparentes.
   3. Le Master écrit l'en-tête de la zone de transfert
      (sprites_write_header) ; le compteur (hud.c) en occupe le début.

   Les images sources sont recopiées de la ROM vers la SDRAM au démarrage
   (sprites_init, par le Master, avant la première image : le Slave ne
   les lit qu'ensuite, son cache n'en contient aucune ligne périmée).
   ========================================================================= */

#define PROP_MIPS 3

typedef struct {
    const Bitmap *mips[PROP_MIPS]; /* pleine taille, moitié, quart */
    uint16_t worldWidth;           /* taille dans le monde, en unités */
    uint16_t worldHeight;
} PropKind;

#define MAX_PROP_KINDS 8
#define MAX_SHOWN 12      /* objets affichés au plus */
#define MAX_PROP_WIDTH 320 /* plus large à l'écran : ignoré */
#define CHUNK 32           /* côté d'un sprite Mega Drive, au plus */

/* Budget de la zone de transfert (shared.h) : tiles et entrées, compteur
   compris. 4 + 168 x 32 + 68 x 8 = 5 924 octets, sous les 6 144. */
#define MD_STAGE_MAX_TILES 168
#define MD_STAGE_MAX_ENTRIES 68

typedef struct {
    int16_t x, y;      /* coin haut gauche à l'écran */
    int16_t w, h;      /* taille à l'écran */
    int16_t visibleH;  /* lignes affichées depuis le haut (crête) */
    uint8_t kind, mip;
    uint32_t stepX, stepY; /* pixels source par pixel d'écran, 16.16 */
    uint16_t firstTile;    /* tile dans la zone de transfert */
    uint8_t firstEntry;    /* entrée (0 : la première des SH2) */
    uint8_t entries;       /* sprites Mega Drive de l'objet */
} PlacedObject;

typedef struct {
    int count;
    PlacedObject objects[MAX_SHOWN];
    uint16_t tiles;  /* tiles de la zone de transfert, compteur compris */
    uint8_t entries; /* entrées, compteur compris */
} SpritePlan;

/* Coûts (cost.h) */
typedef struct {
    uint32_t objects; /* objets fabriqués */
    uint32_t tiles;   /* tiles écrites */
} SpriteCost;

static inline uint32_t sprite_cycles(const SpriteCost *c) {
    return c->objects * CYCLES_SPRITE_OBJECT + c->tiles * CYCLES_SPRITE_TILE;
}

/* Recopie les images des objets en SDRAM ; renvoie les types d'objets
   qui pointent dessus (même ordre que 'kinds'). */
const PropKind *sprites_init(const PropKind *kinds, int count);

/* Les sprites d'un rectangle d'objet à l'écran : un sprite par carré de
   CHUNK pixels qui touche l'écran. Commun au plan, au dessin et aux
   tests. */
static inline int sprites_chunk_shown(int x, int y, int w, int h) {
    return x < SCREEN_WIDTH && x + w > 0 && y < 224 && y + h > 0;
}

void sprites_plan(const Track *track, const PropKind *kinds,
                  const View *view, const SegmentView *views,
                  SpritePlan *plan);

/* Fabrique les objets de rang (cpu, cpu + 2, ...) du plan */
void sprites_draw(const SpritePlan *plan, const PropKind *kinds, int cpu,
                  SpriteCost *cost);

/* En-tête de la zone de transfert : tiles et entrées du plan */
void sprites_write_header(const SpritePlan *plan);

#endif
