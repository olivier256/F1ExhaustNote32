#ifndef PROJECT_H
#define PROJECT_H

#include <stdint.h>

#include "cost.h"
#include "world.h"

/* =========================================================================
   Projection du circuit : ce que montre chaque ligne de l'écran.

   Pour chaque ligne d'écran, soit la route (centre et demi-largeur, en
   pixels 16.16, et la couleur des bandes du segment qu'elle montre), soit
   le décor de fond (rien ne la couvre : ciel, montagnes, ou sol au-delà
   d'une crête).

   Méthode (celle de Jake Gordon, « How to build a racing game ») : on
   projette le début de chacun des DRAW_DISTANCE segments devant la
   caméra, du plus proche au plus lointain. Un segment couvre les lignes
   entre la projection de son début (en bas) et celle de sa fin (en
   haut) ; on interpole linéairement le centre et la largeur d'une ligne à
   l'autre. Une ligne déjà couverte par un segment plus proche le reste :
   c'est ce qui cache la route derrière une crête. Chaque ligne est donc
   décrite une seule fois, et dessinée une seule fois.
   ========================================================================= */

/* Ce que le 68000 envoie pour une image (registres COMM, voir shared.h) */
typedef struct {
    uint32_t position; /* distance de la caméra le long du circuit */
    int16_t playerX;   /* écart latéral de la voiture (et de la caméra) */
    uint16_t skyX;     /* défilement horizontal du décor de fond */
} View;

enum { LINE_SKY = 0, LINE_ROAD = 1 };

typedef struct {
    int32_t x;     /* centre de la route, en pixels 16.16 */
    int32_t w;     /* demi-largeur de la route, en pixels 16.16 */
    uint8_t type;  /* LINE_SKY ou LINE_ROAD */
    uint8_t flags; /* flags du segment montré (SEGMENT_DARK) */
    uint16_t unused;
} RoadLine;

/* Décrit les lignes firstLine, firstLine + lineStep... (< height) de
   l'image vue depuis 'view' ; les autres entrées de 'lines' ne sont pas
   touchées. lineStep vaut 1 ou 2 : chaque CPU décrit ses lignes, et
   obtient exactement les valeurs qu'aurait données une description de
   tout l'écran. */
void project_lines(const Track *track, const View *view, RoadLine *lines,
                   int height, int firstLine, int lineStep, ProjectCost *cost);

#endif
