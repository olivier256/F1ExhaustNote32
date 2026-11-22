#ifndef RACE_H
#define RACE_H

#include <stdint.h>

#include "world.h"

/* =========================================================================
   Moteur de course pseudo-3D pour la 32X.

   La 32X se conduit comme un VDP spécialisé pour le 68000 :
   - le 68000 fait avancer la voiture et écrit sa position dans les
     registres COMM 4 à 6 (voir shared.h) ;
   - les deux SH2 projettent le circuit et écrivent, à chaque image, la
     table de lignes du VDP : chaque ligne d'écran montre l'une des lignes
     de route préparées au démarrage (render.h). Le Master écrit au HBlank
     le décalage d'écran et les couleurs claires ou sombres de la ligne
     suivante.

   Le moteur fournit les points d'entrée des SH2 (master_main, slave_main
   et leurs interruptions, voir pixel/game.h). Le programme qui l'utilise
   fournit le contenu (ci-dessous) et le programme 68000 (md_vint).
   ========================================================================= */

typedef struct {
    const Track *track; /* le circuit */
} RaceContent;

/* Fourni par le programme : ce que le moteur affiche */
extern const RaceContent race_content;

#endif
