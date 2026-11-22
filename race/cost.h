#ifndef COST_H
#define COST_H

#include <stdint.h>

/* =========================================================================
   Modèle de coûts : cycles SH2 estimés.

   Le simulateur exécute le moteur en code natif : il ne mesure pas le
   temps SH2. Le moteur compte donc ce qu'il fait (segments dessinés,
   lignes de route) et multiplie par un coût unitaire en cycles. Le résultat est
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

/* Choix des lignes (render.c), par CPU : une écriture par ligne, dans la
   table de lignes, comptée par le simulateur. */
typedef struct {
    uint32_t roadLines; /* lignes de route */
    uint32_t skyLines;  /* lignes de ciel ou de sol */
} RenderCost;

/* Coûts unitaires, en cycles SH2, hors écritures dans le frame buffer.
   Étalonnés par tools/sh2cycles (moindres carrés sur 360 images, à -2 % /
   +2 % près), code compilé en C, cache en 2 voies + RAM, code de chaque
   image (project.c, render_line, hud.c) en RAM interne (1 972 octets sur
   2 048).

   Chaque CPU traite toujours 112 lignes, route ou ciel, et projette 160
   points : l'ajustement ne peut pas séparer ces coûts fixes.
   CYCLES_FRAME réunit la projection des points et le coût d'une ligne de
   ciel pour les 112 lignes ; CYCLES_ROAD_LINE est le surcoût d'une ligne
   de route (description, choix de la largeur, du décalage). */
#define CYCLES_FRAME 54472  /* par image et par CPU */
#define CYCLES_SEGMENT 363  /* segment dessiné : division, pentes */
#define CYCLES_ROAD_LINE 18 /* surcoût d'une ligne de route */

/* Interruptions du Master, par image : une H_INT par ligne affichée (elle
   écrit le décalage d'écran de la ligne suivante, et quatre couleurs
   quand la bande change) et une V_INT. Entrée dans l'exception, routine,
   RTE : ESTIMATION, d'après les attentes du 32X Hardware Manual (registres
   du VDP et palette : 5 attentes), non mesurée par le banc (qui ne simule
   pas les interruptions). */
#define CYCLES_INTERRUPT 100   /* SFT compris (environ 7 cycles) */
#define CYCLES_PALETTE_SWAP 30 /* les quatre couleurs, en plus (7 chacune) */

/* Le compteur en sprites Mega Drive (hud.c), Master, par image : étalonné
   par le banc (170 écritures dans le frame buffer, comptées à part) */
#define CYCLES_HUD 1941

/* Objets mis à l'échelle (sprites.c, SpriteCost), par CPU : ESTIMATIONS,
   pas encore mesurées par le banc. Le plan (calculé par chaque CPU)
   compte dans l'objet. */
#define CYCLES_SPRITE_OBJECT 800 /* plan, table des colonnes, entrées */
#define CYCLES_SPRITE_TILE 700   /* 64 pixels : 2 lectures et 4 décalages
                                    chacun, 16 écritures comptées à part */

static inline uint32_t cost_cycles(const ProjectCost *p, const RenderCost *r) {
    return CYCLES_FRAME + p->segments * CYCLES_SEGMENT +
           r->roadLines * CYCLES_ROAD_LINE;
}

#endif
