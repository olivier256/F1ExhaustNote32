#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

/* =========================================================================
   Le monde : le circuit et la caméra.

   Partagé par les trois processeurs (le 68000 fait avancer la voiture sur
   le circuit, les SH2 le dessinent) et par tools/track2c, qui produit le
   circuit à la compilation.

   Repère (unités du monde, entiers) :
   - z : la distance le long du circuit, de 0 à la longueur du circuit ;
   - x : l'écart latéral par rapport au centre de la route (positif à
     droite) ;
   - y : la hauteur (positive vers le haut).
   Le circuit est une suite de segments de SEGMENT_LENGTH unités. Chacun
   a une hauteur (celle de son début) et une courbure. Comme dans les
   moteurs pseudo-3D classiques, la courbure n'est pas une vraie
   géométrie : elle fait glisser la route sur le côté, de plus en plus
   vite à mesure qu'on s'éloigne (voir race/project.c).
   ========================================================================= */

/* ---- Segments ------------------------------------------------------------ */

#define SEGMENT_SHIFT 8
#define SEGMENT_LENGTH (1 << SEGMENT_SHIFT) /* unités du monde */

/* Bandes : RUMBLE_SEGMENTS segments clairs, puis autant de sombres
   (herbe, bitume, bordures blanches puis rouges, lignes blanches sur les
   segments clairs seulement). Fixé par tools/track2c dans 'flags'. */
#define RUMBLE_SEGMENTS 3
#define SEGMENT_DARK 0x01

typedef struct {
    int32_t y;     /* hauteur du début du segment */
    int16_t curve; /* courbure : ce que gagne, d'un segment au suivant, le
                      glissement latéral de la route, en 1/256 d'unité */
    uint8_t flags; /* SEGMENT_DARK */
    uint8_t unused;
} Segment;

typedef struct {
    uint16_t count;          /* segments ; le circuit boucle */
    const Segment *segments; /* le premier commence à la hauteur 0, et le
                                circuit y revient */
} Track;

/* Longueur du circuit, en unités */
static inline uint32_t track_length(const Track *t) {
    return (uint32_t)t->count << SEGMENT_SHIFT;
}

/* Segment suivant, en bouclant (sans modulo : pas de division sur SH2) */
static inline unsigned track_next(const Track *t, unsigned segment) {
    return segment + 1 == t->count ? 0 : segment + 1;
}

/* Hauteur de la route à la distance z (0 <= z < longueur du circuit),
   interpolée entre le début du segment et celui du suivant */
static inline int32_t track_height(const Track *t, uint32_t z) {
    const unsigned s = z >> SEGMENT_SHIFT;
    const int32_t frac = (int32_t)(z & (SEGMENT_LENGTH - 1));
    const int32_t y0 = t->segments[s].y;
    const int32_t y1 = t->segments[track_next(t, s)].y;
    return y0 + (((y1 - y0) * frac) >> SEGMENT_SHIFT);
}

/* ---- Caméra et écran -----------------------------------------------------
   Projection : un point à la distance z devant la caméra, à l'écart x et
   à la hauteur y par rapport à elle, apparaît en
       X = SCREEN_CENTER_X + x * FOCAL / z
       Y = SCREEN_CENTER_Y - y * FOCAL / z
   FOCAL = 128 : 102 degrés de champ horizontal sur 320 pixels. La caméra
   ne s'incline pas : l'horizon (z infini) reste sur la ligne
   SCREEN_CENTER_Y, quelle que soit la pente. */

#define SCREEN_WIDTH 320
#define SCREEN_CENTER_X 160
#define SCREEN_CENTER_Y 96
#define FOCAL 128

#define ROAD_HALF_WIDTH 768 /* demi-largeur de la route */
#define CAMERA_HEIGHT 640   /* au-dessus de la route, sous la voiture */
#define PLAYER_Z 720        /* la voiture, devant la caméra */
#define NEAR_Z 256          /* plus près, on ne projette pas */
#define DRAW_DISTANCE 160   /* segments dessinés */

#endif
