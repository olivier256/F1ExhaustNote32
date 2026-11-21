#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

/* =========================================================================
   Ce que se partagent les trois processeurs.

   1. Registres COMM (pixel/interface.h) : 8 mots visibles du 68000 et des
      deux SH2, sans cache. SuperVDP s'en sert comme des registres d'un
      VDP : le 68000 y écrit les défilements, les SH2 dessinent.
   2. La scène de l'image en cours (Scene) : relevée par le Master au
      début de l'image, lue par le Slave, en mémoire partagée (SDRAM).
   ========================================================================= */

/* ---- Registres COMM ----------------------------------------------------- */

#define COMM_FRAME 1      /* Master -> Slave : image à dessiner (1, 2, ...) */
#define COMM_SLAVE_DONE 2 /* Slave -> Master : dernière image terminée */

/* Registres de défilement : écrits par le 68000, relevés par le Master une
   fois par image. Valeurs signées quelconques : le plan boucle. */
#define COMM_HSCROLL_A 4 /* colonne du plan A au bord gauche de l'écran */
#define COMM_HSCROLL_B 5 /* colonne du plan B */
#define COMM_VSCROLL_A 6 /* ligne du plan A en haut de l'écran */
#define COMM_VSCROLL_B 7 /* ligne du plan B */
/* COMM 0 et 3 : libres */

/* ---- Scène ---------------------------------------------------------------
   Tout ce qu'il faut pour dessiner une image : les deux CPU dessinent avec
   les mêmes valeurs, même si le 68000 les change en cours d'image. */

typedef struct {
    int16_t hScrollA, hScrollB;
    int16_t vScrollA, vScrollB;
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
