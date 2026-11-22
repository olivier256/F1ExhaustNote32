#ifndef HUD_H
#define HUD_H

#include <stdint.h>

/* =========================================================================
   Compteur en sprites Mega Drive : la distance parcourue sur le circuit,
   en cinq chiffres, en haut à gauche de l'écran.

   C'est le premier client du chemin SH2 -> frame buffer -> DMA -> VRAM
   -> sprites Mega Drive : le Master fabrique à chaque image les tiles
   (4 bits par pixel) et les entrées de la table des sprites, dans la zone
   de transfert du frame buffer (shared.h). Les chiffres changent à chaque
   image : tout repart par le DMA, à chaque VBlank.

   Deux sprites : 4 x 2 cellules (les chiffres 1 à 4) et 1 x 2 (le
   cinquième). Les tiles d'un sprite se suivent colonne par colonne : un
   chiffre (8 x 16 pixels) occupe exactement une colonne de deux tiles.
   Chiffres clairs (couleur 1) sur fond sombre (couleur 2), palette 0.
   ========================================================================= */

#define HUD_DIGITS 5
#define HUD_X 16 /* coin haut gauche, à l'écran */
#define HUD_Y 12
#define HUD_COLOR_DIGIT 1
#define HUD_COLOR_BACK 2
/* Leurs couleurs (palette 0 des sprites), écrites en CRAM par le 68000 :
   0000 bbb0 ggg0 rrr0 */
#define HUD_CRAM_DIGIT 0x0EEE /* blanc */
#define HUD_CRAM_BACK 0x0400  /* bleu nuit */

/* Valeur affichée pour une position sur le circuit : une unité tous les
   16 du monde */
static inline uint32_t hud_value(uint32_t position) { return position >> 4; }

/* Index de couleur du pixel (x, y) d'un chiffre de 8 x 16 : police de
   3 x 5 doublée, avec une marge */
uint8_t hud_digit_pixel(unsigned digit, int x, int y);

/* Écrit dans la zone de transfert du frame buffer de dessin les tiles et
   les entrées de la table des sprites du compteur. Renvoie le nombre
   d'octets que le DMA transférera. */
uint32_t hud_build(uint32_t position);

#endif
