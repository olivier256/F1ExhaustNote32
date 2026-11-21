#ifndef COST_H
#define COST_H

#include <stdint.h>

/* =========================================================================
   Modèle de coûts : cycles SH2 estimés.

   Le simulateur exécute le moteur en code natif : il ne mesure pas le
   temps SH2. Le moteur compte donc ce qu'il fait (lignes, segments,
   morceaux de tile lus, mots copiés selon le chemin...), et multiplie par
   un coût unitaire en cycles. Le résultat est déclaré au simulateur
   (vdp_count_cpu_cycles), qui ajoute lui-même le coût des écritures dans
   le frame buffer (3 cycles, 5 si le FIFO est plein) et l'affiche avec
   VDP_STATS=1.

   Les coûts unitaires sont ÉTALONNÉS par le banc tools/sh2cycles : le
   vrai code de compose.c, compilé pour SH2 (gcc 13.2 -m2 -O2), exécuté
   sous qemu avec un modèle de temps du SH2 (cycles par instruction,
   branchements, dépendances, cache de 4 Ko, SDRAM). Ajustement par
   moindres carrés sur 360 images : le modèle ci-dessous reproduit les
   temps du banc à -3 % / +6 % près. Les refaire après toute modification
   de compose.c : tools/sh2cycles/run.sh imprime les nouvelles valeurs.

   Chaque ligne écrit exactement 160 mots : le coût d'un mot copié depuis
   une source paire ne se distingue pas du coût fixe d'une ligne. Il est
   compté dans CYCLES_LINE ; les coûts par mot sont des surcoûts par
   rapport à lui.

   Sur SH2, les compteurs disparaissent (sauf compilation avec
   SUPERVDP_COST) : le moteur n'est pas ralenti par sa propre mesure.
   ========================================================================= */

#if !defined(__sh__) || defined(SUPERVDP_COST)
#define SUPERVDP_COST_ENABLED 1
#define COST_ADD(cost, field, n) ((cost)->field += (uint32_t)(n))
#else
#define SUPERVDP_COST_ENABLED 0
#define COST_ADD(cost, field, n) ((void)0)
#endif

typedef struct {
    uint32_t lines;       /* lignes composées */
    uint32_t aTiles;      /* tiles de A examinées pour former les segments */
    uint32_t runs;        /* segments dessinés (copie A, copie B, fusion) */
    uint32_t tileChunks;  /* morceaux de ligne de tile lus (un par tile
                             traversée et par plan) : chacun charge une
                             ligne du cache depuis la SDRAM */
    uint32_t wordsEven;   /* mots copiés, source paire (un MOV.W) */
    uint32_t wordsOdd;    /* mots copiés, source impaire (deux MOV.B) */
    uint32_t wordsMerged; /* mots fusionnés pixel par pixel (A sur B) */
} ComposeCost;

/* Coûts unitaires, en cycles SH2, hors écritures dans le frame buffer.
   Étalonnés par tools/sh2cycles (2026-11-21, compose.c de la version
   « deux plans superposés »). */
#define CYCLES_LINE 1930      /* fixe par ligne, dont 160 mots source paire */
#define CYCLES_A_TILE 41      /* tile de A examinée (classe, regroupement) */
#define CYCLES_RUN 33         /* segment : choix du chemin, positions */
#define CYCLES_TILE_CHUNK 69  /* morceau de tile : adresse, cache, boucle */
#define CYCLES_WORD_EVEN 0    /* compté dans CYCLES_LINE */
#define CYCLES_WORD_ODD 16    /* surcoût d'un mot source impaire */
#define CYCLES_WORD_MERGED 92 /* surcoût d'un mot fusionné */

static inline uint32_t cost_cycles(const ComposeCost *c) {
    return c->lines * CYCLES_LINE + c->aTiles * CYCLES_A_TILE +
           c->runs * CYCLES_RUN + c->tileChunks * CYCLES_TILE_CHUNK +
           c->wordsEven * CYCLES_WORD_EVEN + c->wordsOdd * CYCLES_WORD_ODD +
           c->wordsMerged * CYCLES_WORD_MERGED;
}

#endif
