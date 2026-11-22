#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "assets_types.h"
#include "cost.h"
#include "project.h"

/* =========================================================================
   Dessin d'une ligne d'écran dans le frame buffer, d'après sa
   description (project.h).

   Chaque mot de la ligne est écrit une seule fois, en 16 bits : 160
   écritures par ligne (161 pour une ligne décalée d'un point).
   - Route : une suite de plages de couleur unie, de gauche à droite :
     herbe, bordure, bitume, ligne, bitume, ligne, bitume, bordure, herbe.
     Les mots à cheval sur deux plages réunissent leurs deux couleurs.
   - Décor : la ligne y du décor de fond (une image qui boucle
     horizontalement), à partir de la colonne skyX. Si skyX est impair, la
     ligne est copiée depuis la colonne paire précédente et affichée
     décalée d'un point (registre SFT) : la copie se fait par mots
     entiers, sans réassembler les pixels. Sous la dernière ligne du
     décor, on prolonge sa couleur de sol (le premier pixel de sa dernière
     ligne) : c'est ce qu'on voit au-delà d'une crête.
   ========================================================================= */

/* ---- Couleurs de la route ------------------------------------------------
   Index de palette réservés, écrits à l'initialisation (road_palette) :
   le décor de fond ne doit pas les utiliser. */
#define ROAD_COLOR_FIRST 240
enum {
    COLOR_GRASS_LIGHT = ROAD_COLOR_FIRST,
    COLOR_GRASS_DARK,
    COLOR_RUMBLE_LIGHT, /* bordure blanche */
    COLOR_RUMBLE_DARK,  /* bordure rouge */
    COLOR_ROAD_LIGHT,
    COLOR_ROAD_DARK,
    COLOR_LANE,
    ROAD_COLOR_END
};
#define ROAD_COLOR_COUNT (ROAD_COLOR_END - ROAD_COLOR_FIRST)

/* Couleurs au format du VDP 32X (0bbbbbgggggrrrrr) */
extern const uint16_t road_palette[ROAD_COLOR_COUNT];

/* ---- Proportions de la route ----------------------------------------------
   En fractions de la demi-largeur w (des décalages, pas de division) :
   - bordure : w / 8, de part et d'autre de la route ;
   - lignes : deux, centrées en +- w x 85 / 256 (un tiers), de largeur
     w / 32 (w / 64 de chaque côté du centre de la ligne). */
#define RUMBLE_SHIFT 3
#define LANE_POS_MUL 85
#define LANE_HALF_SHIFT 6

/* Dessine la ligne y de l'écran, qui commence à 'offset' octets du début
   du frame buffer de dessin (fb_offset(0, y)). 'sky' : le décor de fond,
   de largeur puissance de 2. Renvoie le décalage d'écran de la ligne (0
   ou 1, valeur du bit SFT). */
int render_line(uint32_t offset, const RoadLine *line, const Bitmap *sky,
                unsigned skyX, int y, RenderCost *cost);

#endif
