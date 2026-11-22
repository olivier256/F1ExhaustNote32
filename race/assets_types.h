#ifndef ASSETS_TYPES_H
#define ASSETS_TYPES_H

/* Pourra être lu par l'assembleur SH2 : seules les constantes lui sont
   visibles, les types C sont réservés au compilateur C. */
#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

/* =========================================================================
   Types des données graphiques, telles que les produit tools/bmp2c.

   Tous les pixels sont des index de palette sur 8 bits (mode Packed Pixel
   du VDP 32X). L'index 0 est réservé à la transparence (plans de tiles,
   sprites) : c'est du logiciel, sans rapport avec le bit 15 des couleurs
   de la palette (« through bit », priorité par rapport à l'écran Mega
   Drive), qui reste à 0.
   ========================================================================= */

/* Les tiles sont carrées, de 16 x 16 pixels : une ligne de tile fait
   16 octets, soit exactement une ligne du cache du SH2. Chaque chargement
   de cache sert donc entièrement à la ligne d'écran en cours, quel que
   soit le CPU qui la dessine. Les tiles commencent à une adresse multiple
   de 16 (TILE_ALIGN), en SDRAM comme dans les données générées.
   TILE_SHIFT est lu par bmp2c et par le moteur. Au plus 4 : la classe
   des lignes d'une tile tient dans 32 bits (voir TileRowClass). */
#define TILE_SHIFT 4
#define TILE_SIZE (1 << TILE_SHIFT)
#define TILE_PIXELS (TILE_SIZE * TILE_SIZE)
#define TILE_ALIGN 16 /* taille d'une ligne du cache SH2 */

#define TILESET_MAX_TILES 65535

/* ---- Classe d'une ligne de tile -------------------------------------------
   Pour chaque tile, bmp2c range la classe de chacune de ses lignes sur
   2 bits : ligne r dans les bits 2r et 2r + 1 d'un mot de 32 bits.
   Le moteur s'en sert pour composer le plan A sur le plan B sans lire ni
   écrire un pixel de trop (voir compose.c). */
#define ROW_TRANSPARENT 0 /* les 16 pixels valent 0 */
#define ROW_OPAQUE 1      /* aucun pixel ne vaut 0 */
#define ROW_MIXED 2       /* les deux */
#define ROW_CLASS_BITS 2
#define ROW_CLASS_MASK 3

#ifndef __ASSEMBLER__

/* Jeu de tiles partagé par les plans : 'count' tiles de TILE_PIXELS
   octets, rangées ligne par ligne, et la classe de leurs lignes. */
typedef struct {
    uint16_t count;
    const uint8_t *pixels;
    const uint32_t *rowClasses; /* une entrée par tile */
} Tileset;

/* Les TILE_SIZE pixels de la ligne 'row' de la tile 'tile' */
static inline const uint8_t *tile_row(const Tileset *tiles, unsigned tile,
                                      unsigned row) {
    return tiles->pixels + (uint32_t)tile * TILE_PIXELS + row * TILE_SIZE;
}

/* Plan : grille de widthTiles x heightTiles numéros de tile (16 bits),
   ligne par ligne. Dimensions quelconques : le plan boucle dans les deux
   directions.

   'classChanges' : pour chaque ligne de pixels du plan, les colonnes de
   tile où la classe de la ligne change (voir TileRowClass), un bit par
   colonne : le bit de la colonne c est mis si sa classe diffère de celle
   de la colonne c - 1 (la colonne 0 est comparée à la dernière). Les bits
   vont du poids fort au poids faible, octet après octet. Les colonnes sont
   suivies de CHANGE_EXTRA_COLUMNS colonnes répétées depuis le début de la
   rangée, pour qu'une ligne d'écran se lise d'un seul tenant, et d'un
   octet de garde. changeBytes octets par ligne (CHANGE_BYTES). */
#define CHANGE_EXTRA_COLUMNS 22 /* une ligne d'écran : 322 pixels */
#define CHANGE_BYTES(widthTiles)                                               \
    (((widthTiles) + CHANGE_EXTRA_COLUMNS + 7) / 8 + 1)

typedef struct {
    uint16_t widthTiles;
    uint16_t heightTiles;
    const uint16_t *cells;
    const uint8_t *classChanges; /* heightTiles * TILE_SIZE lignes */
    uint16_t changeBytes;        /* octets par ligne, CHANGE_BYTES() */
} Tilemap;

/* Image brute (« bitmap » ou « sprite » dans le manifeste de bmp2c) :
   width x height pixels, ligne par ligne, alignés sur 4 octets. Le décor
   de fond en est une ; les sprites mis à l'échelle en seront aussi. */
typedef struct {
    uint16_t width;
    uint16_t height;
    const uint8_t *pixels;
} Bitmap;

/* Classe de la ligne 'row' de la tile 'tile' */
static inline int tile_row_class(const Tileset *tiles, unsigned tile, int row) {
    return (int)(tiles->rowClasses[tile] >> (row * ROW_CLASS_BITS)) &
           ROW_CLASS_MASK;
}

#endif

/* Palette : 256 couleurs au format du VDP 32X, 0bbbbbgggggrrrrr
   (le bit 15, priorité, est laissé à 0). */
#define PALETTE_COLORS 256

#endif
