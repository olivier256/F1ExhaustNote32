#ifndef COMPOSE_H
#define COMPOSE_H

#include <stdint.h>

#include "assets_types.h"
#include "cost.h"

/* =========================================================================
   Composition d'une ligne d'écran : plan A (devant, index 0 transparent)
   sur plan B (derrière, opaque).

   Chaque mot de la ligne (deux pixels) est écrit une seule fois, en
   16 bits, à une adresse paire : 160 écritures par ligne, 35 840 par
   image, quels que soient les défilements. Aucun tampon de ligne : chaque
   pixel est lu une fois, dans le plan qui le montre (voir compose.c).
   ========================================================================= */

/* Compose la ligne qui commence à l'offset 'offset' du frame buffer de
   dessin (voir fb_offset) :
   - plan A : sa ligne worldAY, à partir de sa colonne scrollAX ;
   - plan B : sa ligne worldBY, à partir de sa colonne scrollBX.
   Les quatre valeurs sont quelconques (négatives, ou plus grandes que le
   plan) : elles sont ramenées dans le plan, qui boucle.
   Les deux plans utilisent le même jeu de tiles. 'cost' reçoit les
   compteurs du modèle de coûts (voir cost.h). */
void compose_line(uint32_t offset, const Tileset *tiles, const Tilemap *planeA,
                  int scrollAX, int worldAY, const Tilemap *planeB,
                  int scrollBX, int worldBY, ComposeCost *cost);

#endif
