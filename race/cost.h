#ifndef COST_H
#define COST_H

#include <stdint.h>

/* =========================================================================
   Modèle de coûts : cycles SH2 estimés.

   Le simulateur exécute le moteur en code natif : il ne mesure pas le
   temps SH2. Le moteur compte donc ce qu'il fait (segments dessinés,
   lignes de route, plages de couleur, morceaux de décor copiés) et
   multiplie par un coût unitaire en cycles. Le résultat est
   déclaré au simulateur (vdp_count_cpu_cycles), qui ajoute lui-même le
   coût des écritures dans le frame buffer (3 cycles, 5 si le FIFO est
   plein) et l'affiche avec VDP_STATS=1.

   Les coûts unitaires viennent du banc tools/sh2cycles : le vrai code de
   project.c et render.c, compilé pour SH2 (gcc 13.2 -m2 -O2), exécuté
   sous qemu avec un modèle de temps du SH2 (voir le README du banc). Les
   refaire après toute modification de ces fichiers : tools/sh2cycles/
   run.sh imprime les nouvelles valeurs.

   Sur SH2, les compteurs disparaissent (sauf compilation avec
   RACE_COST) : le moteur n'est pas ralenti par sa propre mesure.
   ========================================================================= */

#if !defined(__sh__) || defined(RACE_COST)
#define RACE_COST_ENABLED 1
#define COST_ADD(cost, field, n) ((cost)->field += (uint32_t)(n))
#else
#define RACE_COST_ENABLED 0
#define COST_ADD(cost, field, n) ((void)0)
#endif

/* Projection (project.c), par CPU. Chaque image projette les
   DRAW_DISTANCE débuts de segment (une division chacun) : ce coût-là est
   le même à chaque image, il est dans CYCLES_FRAME. */
typedef struct {
    uint32_t segments; /* segments qui couvrent au moins une ligne (une
                          division, l'interpolation) */
} ProjectCost;

/* Dessin (render.c), par CPU. Les mots écrits ne sont pas comptés ici :
   chaque ligne en écrit 160 (161 décalée), et le simulateur compte les
   écritures. Leur boucle est dans le coût de la ligne. */
typedef struct {
    uint32_t roadLines; /* lignes de route */
    uint32_t skyLines;  /* lignes de décor (ou de sol, sous le décor) */
    uint32_t runs;      /* plages de couleur non vides (route, sol) */
    uint32_t skyChunks; /* morceaux de ligne de décor copiés (1 ou 2) */
} RenderCost;

/* Coûts unitaires, en cycles SH2, hors écritures dans le frame buffer.
   Étalonnés par tools/sh2cycles (moindres carrés sur 360 images, à -4 % /
   +4 % près), code compilé en C, cache en 2 voies + RAM, render.c en RAM
   interne.

   Chaque CPU dessine toujours 112 lignes, route ou décor : la somme
   roadLines + skyLines ne varie pas, et les 160 points projetés non plus.
   L'ajustement ne peut donc pas séparer ces coûts fixes : CYCLES_FRAME
   réunit la projection des points et le coût d'une ligne de décor (la
   copie de ses 160 mots) pour les 112 lignes ; CYCLES_ROAD_LINE est le
   surcoût d'une ligne de route par rapport à une ligne de décor. Le
   profil du banc donne la vraie répartition (voir VERSION.md). */
#define CYCLES_FRAME 180061  /* par image et par CPU */
#define CYCLES_SEGMENT 398   /* segment dessiné : division, pentes */
#define CYCLES_ROAD_LINE 263 /* surcoût d'une ligne de route */
#define CYCLES_RUN 17        /* plage de couleur non vide */
#define CYCLES_SKY_CHUNK 36  /* morceau de décor (le second : bouclage) */

/* Interruptions du Master, par image : une H_INT par ligne affichée (elle
   écrit le décalage d'écran de la ligne suivante) et une V_INT. Entrée
   dans l'exception, routine, RTE : ESTIMATION, non mesurée par le banc
   (qui ne simule pas les interruptions). */
#define CYCLES_INTERRUPT 40

static inline uint32_t cost_cycles(const ProjectCost *p, const RenderCost *r) {
    return CYCLES_FRAME + p->segments * CYCLES_SEGMENT +
           r->roadLines * CYCLES_ROAD_LINE + r->runs * CYCLES_RUN +
           r->skyChunks * CYCLES_SKY_CHUNK;
}

#endif
