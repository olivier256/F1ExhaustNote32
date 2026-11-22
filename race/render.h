#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "cost.h"
#include "project.h"

/* =========================================================================
   Dessin par la table de lignes du VDP 32X.

   Les CPU ne dessinent plus les pixels de la route à chaque image : toutes
   les lignes dont l'écran peut avoir besoin sont préparées une fois pour
   toutes, au démarrage, dans chacun des deux frame buffers. À chaque
   image, il suffit d'écrire la table de lignes (un mot par ligne d'écran :
   l'adresse, en mots, de la ligne à montrer), et de dire au Master ce
   qu'il doit écrire au HBlank qui précède chaque ligne : le décalage
   d'écran (SFT) et, si elles changent, quatre couleurs de la palette.

   Lignes préparées (render_prepare) :
   - la route, pour ROAD_WIDTHS demi-largeurs : w_k = k x ROAD_WIDTH_STEP
     (1,2 pixel), k de 0 à 127. Sur une route plate, la demi-largeur
     grandit de 1,2 pixel par ligne d'écran : chaque ligne d'écran a la
     sienne. Les routes se suivent en mémoire, séparées par GRASS_MARGIN
     (320) pixels d'herbe : une fenêtre de 320 pixels autour d'une route ne
     voit jamais la suivante, quel que soit le décalage. Le début de cette
     herbe sert aussi de ligne d'herbe seule, quand la route sort de
     l'écran. Environ 66 Ko ;
   - le ciel : SKY_BANDS lignes unies, une par bande de 8 lignes d'écran
     (dégradé), et une ligne de sol, pour ce qu'on voit au-delà d'une
     crête. En attendant les plans de la Mega Drive.

   Choix d'une ligne (render_line) :
   - route : k = la demi-largeur arrondie à un multiple de 1,2 pixel (un
     produit, pas de division), puis l'octet de la route k qui doit tomber
     sur le pixel 0 de l'écran : centre de la route k - centre à l'écran.
     La table ne donne que des adresses de mots : si cet octet est
     impair, on pointe l'octet d'avant et la ligne est affichée décalée
     d'un point (SFT) ;
   - les bandes claires et sombres ne sont pas dans les pixels : la route
     n'utilise que quatre index (herbe, bordure, bitume, ligne blanche),
     dont le Master change les couleurs au HBlank (road_light, road_dark ;
     sur les bandes sombres, la ligne blanche prend la couleur du bitume).

   Une seule écriture dans le frame buffer par ligne d'écran : 224 par
   image.
   ========================================================================= */

/* ---- Couleurs --------------------------------------------------------------
   Index de palette. La route n'en utilise que quatre, dont les couleurs
   changent d'une ligne à l'autre (bandes claires et sombres). */
#define SKY_BAND_SHIFT 3 /* 8 lignes d'écran par bande */
#define SKY_BANDS (SCREEN_CENTER_Y >> SKY_BAND_SHIFT)
#define COLOR_SKY_FIRST 1                          /* SKY_BANDS couleurs */
#define COLOR_GROUND (COLOR_SKY_FIRST + SKY_BANDS) /* au-delà d'une crête */

enum {
    COLOR_GRASS = 240,
    COLOR_RUMBLE, /* bordure : blanche, ou rouge */
    COLOR_ROAD,   /* bitume */
    COLOR_LANE,   /* ligne blanche, ou bitume sur les bandes sombres */
    ROAD_COLOR_END
};
#define COLOR_ROAD_FIRST COLOR_GRASS
#define ROAD_COLORS (ROAD_COLOR_END - COLOR_ROAD_FIRST)

/* Couleurs au format du VDP 32X (0bbbbbgggggrrrrr) */
extern const uint16_t sky_palette[SKY_BANDS + 1]; /* bandes, puis le sol */
extern const uint16_t road_light[ROAD_COLORS];
extern const uint16_t road_dark[ROAD_COLORS];

/* ---- Proportions de la route ----------------------------------------------
   En fractions de la demi-largeur w (des décalages, pas de division) :
   - bordure : w / 8, de part et d'autre de la route ;
   - lignes : deux, centrées en +- w x 85 / 256 (un tiers), de largeur
     w / 32 (w / 64 de chaque côté du centre de la ligne). */
#define RUMBLE_SHIFT 3
#define LANE_POS_MUL 85
#define LANE_HALF_SHIFT 6

/* ---- Lignes préparées ------------------------------------------------------
 */

#define ROAD_WIDTHS 128
/* Pas de la demi-largeur, en pixels 16.16 : ROAD_HALF_WIDTH x 65536 /
   CAMERA_HEIGHT (1,2 pixel), arrondi par défaut */
#define ROAD_WIDTH_STEP ((ROAD_HALF_WIDTH * 65536) / CAMERA_HEIGHT)
/* k = arrondi(w / ROAD_WIDTH_STEP) = (w x ROAD_WIDTH_INV + 2^31) >> 32 :
   ROAD_WIDTH_INV = 2^32 / ROAD_WIDTH_STEP, arrondi */
#define ROAD_WIDTH_INV 54614u
#define GRASS_MARGIN SCREEN_WIDTH

/* Où sont les lignes préparées : offsets en octets depuis le début du
   frame buffer (table de lignes comprise). Le même plan dans les deux
   frame buffers. Calculé par chaque CPU au démarrage (render_layout). */
typedef struct {
    uint32_t center[ROAD_WIDTHS]; /* centre de la route k */
    uint32_t left[ROAD_WIDTHS];   /* premier octet de la route k */
    uint32_t right[ROAD_WIDTHS];  /* après le dernier */
    uint32_t grass;               /* 320 octets d'herbe seule */
    uint32_t sky[SKY_BANDS + 1];  /* lignes du ciel, puis du sol */
    uint32_t end;                 /* après la dernière ligne préparée */
} RoadLayout;

/* Demi-largeur de la route k, en pixels 16.16 */
static inline int32_t road_half_width(unsigned k) {
    return (int32_t)(k * (uint32_t)ROAD_WIDTH_STEP);
}

/* Index de couleur du pixel à 'd' pixels du centre d'une route de
   demi-largeur w (16.16), centre sur un pixel entier */
uint8_t road_pixel(int32_t w, int d);

void render_layout(RoadLayout *layout);

/* Écrit toutes les lignes préparées dans le frame buffer de dessin (une
   fois par frame buffer, au démarrage). Renvoie 0 si elles ne tiennent
   pas dans le frame buffer. */
int render_prepare(const RoadLayout *layout);

/* ---- Choix d'une ligne -----------------------------------------------------
 */

/* Ce que le Master écrit au HBlank qui précède la ligne */
#define LINE_SFT 0x01  /* bit SFT : ligne décalée d'un point */
#define LINE_DARK 0x02 /* couleurs sombres de la route (road_dark) */

/* Écrit l'entrée y de la table de lignes du frame buffer de dessin, pour
   la ligne décrite par 'line'. Renvoie LINE_SFT | LINE_DARK, selon. */
unsigned render_line(int y, const RoadLine *line, const RoadLayout *layout,
                     RenderCost *cost);

#endif
