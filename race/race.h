#ifndef RACE_H
#define RACE_H

#include <stdint.h>

#include "assets_types.h"
#include "world.h"

/* =========================================================================
   Moteur de course pseudo-3D pour la 32X.

   La 32X se conduit comme un VDP spécialisé pour le 68000 :
   - le 68000 fait avancer la voiture et écrit sa position dans les
     registres COMM 4 à 7 (voir shared.h) ;
   - les deux SH2 dessinent chaque image : la route projetée ligne par
     ligne, et le décor de fond au-dessus, une ligne sur deux chacun.

   Le moteur fournit les points d'entrée des SH2 (master_main, slave_main
   et leurs interruptions, voir pixel/game.h). Le programme qui l'utilise
   fournit le contenu (ci-dessous) et le programme 68000 (md_vint).
   ========================================================================= */

typedef struct {
    const Track *track;      /* le circuit */
    const Bitmap *sky;       /* décor de fond, largeur puissance de 2 ;
                                index de palette < ROAD_COLOR_FIRST */
    const uint16_t *palette; /* PALETTE_COLORS couleurs ; celles de la
                                route sont remplacées (render.h) */
} RaceContent;

/* Fourni par le programme : ce que le moteur affiche */
extern const RaceContent race_content;

#endif
