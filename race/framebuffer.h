#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

#include "vdp.h"

/* =========================================================================
   Frame buffer du VDP 32X en mode Packed Pixel (un octet par pixel).

   La DRAM commence par la table de lignes : un mot par ligne affichée,
   qui donne l'adresse (en mots) des pixels de la ligne. On réserve
   256 mots (512 octets), comme le 32X Hardware Manual. Le reste, jusqu'à
   128 Ko, est libre : le moteur y prépare les lignes qu'il montrera
   (render.h), et chaque image ne fait que réécrire la table.

   Toutes les écritures du moteur dans le frame buffer passent par
   vdp_dram_write*() ou vdp_overwrite_write*(), pour être comptées par le
   simulateur (voir vdp.h), table de lignes comprise.
   ========================================================================= */

#define FB_WIDTH VDP_SCREEN_WIDTH
#define FB_LINE_TABLE_BYTES 512

/* Fait pointer toutes les lignes de la table du frame buffer de dessin
   vers la même ligne (octet 'offset', pair) : l'écran du démarrage,
   avant la première image. */
void fb_init_line_table(int screenHeight, uint32_t offset);

#endif
