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
   branchements, dépendances, cache, SDRAM). Le cache est supposé en mode
   « 2 voies + RAM », le code de compose.c dans la RAM interne (voir
   tools/sh2cycles/bench_ram.ld). Ajustement par moindres carrés, à coûts
   positifs ou nuls, sur 360 images : le modèle ci-dessous reproduit les
   temps du banc à -7 % / +4 % près. Les refaire après toute modification
   de compose.c : tools/sh2cycles/run.sh imprime les nouvelles valeurs.

   Chaque ligne écrit exactement 160 mots, et examine presque toujours 20
   ou 21 tiles de A : le coût fixe d'une ligne (dont les mots copiés
   depuis une source paire) ne se distingue pas de celui des tiles
   examinées. Il est compté dans CYCLES_A_TILE ; les coûts par mot sont
   des surcoûts par rapport à un mot copié depuis une source paire.

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
    uint32_t aTiles;      /* tiles de A couvertes par la ligne (20 ou 21,
                             plus l'entamée) : la part fixe de la ligne */
    uint32_t runs;        /* segments dessinés (copie A, copie B, fusion) */
    uint32_t tileChunks;  /* morceaux de ligne de tile lus (un par tile
                             traversée et par plan) : chacun charge une
                             ligne du cache depuis la SDRAM */
    uint32_t wordsEven;   /* mots copiés, source paire (un MOV.W) */
    uint32_t wordsOdd;    /* mots copiés, source impaire (deux MOV.B) */
    uint32_t wordsMerged; /* mots fusionnés pixel par pixel (A sur B) */
} ComposeCost;

/* Coûts unitaires, en cycles SH2, hors écritures dans le frame buffer.
   Étalonnés par tools/sh2cycles sur le code de la console : compose.c
   compilé avec SUPERVDP_ASM=1 (fusion en assembleur, merge_sh2.S),
   décalage d'écran choisi ligne par ligne, cache en 2 voies + RAM. */
#define CYCLES_LINE 0         /* compté dans CYCLES_A_TILE */
#define CYCLES_A_TILE 14      /* part fixe de la ligne, répartie par tile */
#define CYCLES_RUN 194        /* segment : formation, choix du chemin */
#define CYCLES_TILE_CHUNK 53  /* morceau de tile : adresse, cache, boucle */
#define CYCLES_WORD_EVEN 0    /* compté dans les coûts ci-dessus */
#define CYCLES_WORD_ODD 3     /* surcoût d'un mot source impaire */
#define CYCLES_WORD_MERGED 17 /* surcoût d'un mot fusionné */

/* Interruptions du Master, par image : une H_INT par ligne affichée (elle
   écrit le décalage d'écran de la ligne suivante) et une V_INT. Entrée
   dans l'exception, routine, RTE : ESTIMATION, non mesurée par le banc
   (qui ne simule pas les interruptions). */
#define CYCLES_INTERRUPT 40

static inline uint32_t cost_cycles(const ComposeCost *c) {
    return c->lines * CYCLES_LINE + c->aTiles * CYCLES_A_TILE +
           c->runs * CYCLES_RUN + c->tileChunks * CYCLES_TILE_CHUNK +
           c->wordsEven * CYCLES_WORD_EVEN + c->wordsOdd * CYCLES_WORD_ODD +
           c->wordsMerged * CYCLES_WORD_MERGED;
}

#endif
