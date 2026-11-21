#ifndef COST_H
#define COST_H

#include <stdint.h>

/* =========================================================================
   Modèle de coûts : cycles SH2 estimés.

   Le simulateur exécute le moteur en code natif : il ne mesure pas le
   temps SH2. Le moteur compte donc ce qu'il fait (mots copiés selon le
   chemin, morceaux de tile lus, segments...), et multiplie par un coût
   unitaire en cycles. Le résultat est déclaré au simulateur
   (vdp_count_cpu_cycles), qui ajoute lui-même le coût des écritures dans
   le frame buffer (3 cycles, 5 si le FIFO est plein) et l'affiche avec
   VDP_STATS=1.

   LES COÛTS UNITAIRES SONT PROVISOIRES : ils sont déduits des suites
   d'instructions SH2 qu'on attend pour chaque chemin, pas mesurés. Ils
   seront étalonnés sur le code SH2 compilé (banc d'essai, émulateur 32X).
   Les écritures dans le frame buffer n'y figurent pas.

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

/* Coûts unitaires, en cycles SH2, hors écriture dans le frame buffer.
   Une ligne du cache chargée depuis la SDRAM : 12 cycles (8 mots en
   rafale, 32X Hardware Manual, « SDRAM Access Time »). */
#define CYCLES_LINE 40        /* appel, plans, parités, boucle */
#define CYCLES_A_TILE 12      /* numéro de tile, classe, regroupement */
#define CYCLES_RUN 20         /* choix du chemin, positions dans A et B */
#define CYCLES_TILE_CHUNK 22  /* adresse de la tile (10) + cache (12) */
#define CYCLES_WORD_EVEN 4    /* MOV.W lu, boucle (DT, BF/S, ADD) */
#define CYCLES_WORD_ODD 7     /* 2 MOV.B, SHLL8, OR, EXTU, boucle */
#define CYCLES_WORD_MERGED 16 /* 4 octets lus, 2 tests, assemblage */

static inline uint32_t cost_cycles(const ComposeCost *c) {
    return c->lines * CYCLES_LINE + c->aTiles * CYCLES_A_TILE +
           c->runs * CYCLES_RUN + c->tileChunks * CYCLES_TILE_CHUNK +
           c->wordsEven * CYCLES_WORD_EVEN + c->wordsOdd * CYCLES_WORD_ODD +
           c->wordsMerged * CYCLES_WORD_MERGED;
}

#endif
