#ifndef MDVDP_H
#define MDVDP_H

#include <stdint.h>

/* =========================================================================
   VDP Mega Drive (émulé), réduit à ce dont le moteur a besoin pour
   l'instant : la VRAM, la palette (CRAM), la table des sprites et
   l'affichage des sprites. Pas encore de plans A et B, ni de fenêtre : le
   fond de l'image Mega Drive est transparent. Module sans thread.

   Le 68000 n'est pas émulé : son programme (demo/md68k.c) appelle les
   fonctions « vu du 68000 » ci-dessous, qui tiennent lieu d'accès aux
   ports du VDP ($C00000, $C00004).

   Sprites (Genesis Technical Overview) : 80 au plus, mode H40. Une entrée
   de la table fait 8 octets :
       0 : ------yy yyyyyyyy   position verticale + 128
       2 : ----hhvv -lllllll   taille (largeur hh, hauteur vv, en cellules
                               de 8 pixels, moins 1), lien vers l'entrée
                               suivante (0 : fin de la liste)
       4 : pccvhnnn nnnnnnnn   priorité, palette, retournements, tile
       6 : -------x xxxxxxxx   position horizontale + 128
   Les tiles d'un sprite se suivent colonne par colonne : de haut en bas,
   puis de gauche à droite. Une tile : 8 x 8 pixels, 4 bits par pixel
   (pixel de gauche dans les 4 bits de poids fort), 32 octets.
   Par ligne : 20 sprites au plus, et 320 pixels de sprites ; au-delà, le
   VDP n'affiche pas la suite (compté dans les statistiques).
   ========================================================================= */

#define MD_VRAM_SIZE 65536
#define MD_CRAM_COLORS 64
#define MD_SPRITES 80
#define MD_SPRITES_PER_LINE 20
#define MD_SPRITE_PIXELS_PER_LINE 320
#define MD_SPRITE_OFFSET 128 /* coordonnées des sprites : écran + 128 */
#define MD_TILE_BYTES 32

/* ---- DMA du VBlank ---------------------------------------------------------
   Pendant le VBlank, le DMA mémoire -> VRAM transfère environ 205 octets
   par ligne en H40 (Genesis Technical Overview, « Access timing » ; à
   confirmer, d'autant que la source est ici le frame buffer 32X, lu avec
   des attentes). Pendant l'affichage, guère plus qu'une boucle du 68000 :
   environ 18 octets par ligne. Le 68000 est arrêté pendant le DMA. */
#define MD_DMA_BYTES_PER_VBLANK_LINE 205
#define MD_DMA_BYTES_PER_DISPLAY_LINE 18

/* ---- Vu du 68000 -----------------------------------------------------------
 */

/* Registre 5 : adresse de la table des sprites en VRAM (multiple de
   1024 en H40) */
void md_vdp_set_sprite_table(uint16_t vramAddress);

/* Écriture d'un mot en VRAM par le port de données (adresse paire) */
void md_vdp_write_vram(uint16_t vramAddress, uint16_t word);

/* Écriture d'une couleur de la CRAM : 0000 bbb0 ggg0 rrr0. Index 0, 16,
   32, 48 : la couleur transparente de chaque palette des sprites. */
void md_vdp_write_cram(int index, uint16_t color);

/* DMA mémoire -> VRAM, depuis le frame buffer 32X vu du 68000
   ($840000 + fbOffset) : le frame buffer qui n'est pas affiché. Il faut
   que le 68000 ait le VDP 32X (bit FM à 0), sinon le simulateur s'arrête.
   Le transfert prend des lignes (voir plus haut) : tant que
   md_vdp_dma_busy() vaut 1, le 68000 est arrêté. Deux DMA de suite
   s'enchaînent. */
void md_vdp_dma_from_32x(uint32_t fbOffset, uint16_t vramAddress,
                         uint32_t bytes);
int md_vdp_dma_busy(void);

/* Lecture d'un mot du frame buffer 32X par le 68000 ($840000 + fbOffset),
   même règle (bit FM à 0) */
uint16_t md_read_32x_fb16(uint32_t fbOffset);

/* ---- Vu de la plate-forme
 * ---------------------------------------------------- */

/* Début de chaque ligne (0 .. 261) : la synchro vidéo fait avancer le DMA */
void md_vdp_begin_line(int line, int inVBlank);

/* Dessine les sprites de la ligne 'line' sur 'rgb' (VDP_SCREEN_WIDTH
   pixels, 0x00RRGGBB), déjà rempli par vdp_render_line. 'through' : pour
   chaque pixel 32X, 1 si sa couleur a le bit de transparence (bit 15). Si
   la Mega Drive est devant (bit PRI, voir vdp.h), un pixel de sprite non
   transparent remplace le pixel 32X ; sinon, il ne se voit qu'à travers un
   pixel 32X transparent. */
void md_vdp_compose_line(int line, uint32_t *rgb, const uint8_t *through,
                         int megaDriveInFront);

/* Diagnostics */
typedef struct {
    unsigned long dmas;         /* transferts */
    uint32_t lastVBlankBytes;   /* octets du dernier VBlank qui en a eu */
    uint32_t maxVBlankBytes;    /* pire VBlank (remis à zéro à la lecture) */
    uint32_t lastVBlankLines;   /* lignes de VBlank qu'ils ont prises */
    unsigned long dmaInDisplay; /* transferts débordant sur l'affichage */
    unsigned long spriteOverflowLines; /* lignes à plus de 20 sprites ou
                                          320 pixels */
} md_vdp_stats;

void md_vdp_get_stats(md_vdp_stats *stats, int resetMax);

/* 1 si un DMA a commencé pendant le VBlank en cours (pour les
   statistiques de vdp.c : l'échange de frame buffer a-t-il lieu dans le
   même VBlank que le transfert vers la Mega Drive ?) */
int md_vdp_dma_in_this_vblank(void);

#endif
