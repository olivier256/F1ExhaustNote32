#ifndef SUPERVDP_H
#define SUPERVDP_H

#include <stdint.h>

#include "assets_types.h"

/* =========================================================================
   SuperVDP : moteur de deux plans de tiles pour la 32X.

   La 32X se conduit comme un VDP pour le 68000 :
   - le 68000 écrit les défilements dans les registres COMM 4 à 7
     (voir shared.h) ;
   - les deux SH2 dessinent chaque image : plan A (devant, index 0
     transparent) sur plan B (derrière), une ligne sur deux chacun.

   SuperVDP fournit les points d'entrée des SH2 (master_main, slave_main,
   et leurs routines V_INT, voir pixel/game.h). Le programme qui l'utilise
   fournit le contenu (ci-dessous) et le programme 68000 (md_vint).
   ========================================================================= */

typedef struct {
    const Tileset *tiles;    /* jeu de tiles commun aux deux plans */
    const Tilemap *planeA;   /* devant ; index 0 transparent */
    const Tilemap *planeB;   /* derrière ; opaque */
    const uint16_t *palette; /* PALETTE_COLORS couleurs */
} SuperVdpContent;

/* Fourni par le programme : ce que SuperVDP affiche */
extern const SuperVdpContent supervdp_content;

#endif
