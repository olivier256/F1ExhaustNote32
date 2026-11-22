#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#include "project.h"

/* =========================================================================
   Ce que se partagent les trois processeurs.

   1. Registres COMM (pixel/interface.h) : 8 mots visibles du 68000 et des
      deux SH2, sans cache. Le moteur s'en sert comme des registres d'un
      VDP : le 68000 y écrit où en est la voiture, les SH2 dessinent.
   2. La scène de l'image en cours (Scene) : relevée par le Master au
      début de l'image, lue par le Slave, en mémoire partagée (SDRAM).
   3. Le circuit (world.h) : des données constantes, en ROM sur la
      console, lues par le 68000 comme par les SH2.
   ========================================================================= */

/* ---- Registres COMM ----------------------------------------------------- */

#define COMM_FRAME 1      /* Master -> Slave : image à dessiner (1, 2, ...) */
#define COMM_SLAVE_DONE 2 /* Slave -> Master : dernière image terminée */

/* Échange avec le 68000 (voir race.c, « Déroulement d'une image ») */
#define COMM_READY                                                             \
    3 /* Master -> 68000 : image prête dans le frame                          \
         buffer de dessin (table de lignes, données pour                      \
         la Mega Drive) ; les SH2 ne touchent plus au VDP                      \
         32X jusqu'à COMM_DMA_DONE */
#define COMM_DMA_DONE                                                          \
    0 /* 68000 -> Master : image transférée vers la Mega                     \
         Drive, VDP 32X rendu aux SH2 (bit FM) */

/* Écrits par le 68000 à chaque VBlank, relevés par le Master une fois par
   image (voir project.h, View) */
#define COMM_POSITION_HI 4 /* distance de la caméra, 16 bits de poids fort */
#define COMM_POSITION_LO 5 /* 16 bits de poids faible */
#define COMM_PLAYER_X 6    /* écart latéral de la voiture, signé */
/* COMM 7 : libre */

/* ---- Données pour la Mega Drive
   ---------------------------------------------- Les SH2 préparent, dans le
   frame buffer de dessin, à MD_STAGE_OFFSET (après les lignes préparées), ce
   que le 68000 transfère par DMA vers la VRAM pendant le VBlank : +0 : nombre
   d'octets de tiles (multiple de 32) +2 : nombre d'entrées de la table des
   sprites +4 : les tiles, puis les entrées de la table des sprites (8 octets
          chacune, au format du VDP Mega Drive, voir pixel/mdvdp.h).
   Les tiles vont en VRAM à partir de la tile MD_SH2_FIRST_TILE ; les
   entrées, à partir de l'entrée MD_RESERVED_SPRITES de la table (les
   premières sont au 68000). La dernière entrée des SH2 a le lien 0. */
#define MD_STAGE_OFFSET 0x14000
#define MD_STAGE_HEADER 4
#define MD_STAGE_MAX_BYTES 6144 /* budget du DMA par VBlank (environ) */
#define MD_SPRITE_TABLE 0xD800  /* en VRAM */
#define MD_SH2_FIRST_TILE 0x100 /* en VRAM : 0x2000 */
#define MD_RESERVED_SPRITES 10

/* ---- Scène ---------------------------------------------------------------
   Tout ce qu'il faut pour dessiner une image : les deux CPU dessinent avec
   les mêmes valeurs, même si le 68000 les change en cours d'image. */

typedef struct {
    View view;
    int16_t drawBuffer; /* frame buffer dessiné : 0 ou 1 (valeur de FS
                           quand il sera affiché) */
} Scene;

/* ---- Mémoire partagée entre les SH2 --------------------------------------
   Chaque SH2 a son propre cache : sans précaution, le Slave peut lire une
   copie périmée de ce que le Master vient d'écrire.
   - Sur 32X : on accède à la donnée par son adresse « cache-through »
     (adresse | 2000 0000h), qui contourne le cache.
   - Sur le simulateur : les threads de l'hôte n'ont pas ce problème de
     cache, mais le processeur (Apple Silicon) peut réordonner les
     écritures : il faut une barrière mémoire.
   SHARED(p) donne le pointeur à utiliser ; SHARED_BARRIER() sépare
   l'écriture des données de celle du signal COMM qui les annonce (et,
   côté lecteur, la lecture du signal de celle des données). */

#if defined(__sh__)
#define SHARED(p) ((__typeof__(p))((uintptr_t)(p) | 0x20000000u))
#define SHARED_BARRIER() __asm__ volatile("" ::: "memory")
#else
#define SHARED(p) (p)
#define SHARED_BARRIER() __sync_synchronize()
#endif

#endif
