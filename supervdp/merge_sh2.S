/* =========================================================================
   merge_sh2.S : fusion pixel par pixel (A s'il est non nul, sinon B),
   en assembleur SH2. Même interface et même résultat que mergeSpan
   (compose.c), qui reste la référence ; utilisée à la place quand
   compose.c est compilé pour le SH2 avec SUPERVDP_ASM=1.

   void merge_span_sh2(uint16_t *d, const PlaneRow *pa, unsigned xa,
                       const PlaneRow *pb, unsigned xb, unsigned n);

   n pixels (pair, au moins 2) : A depuis sa colonne xa, B depuis sa
   colonne xb, écrits en n / 2 mots à partir de d (adresse paire).

   Même découpage que le C : on avance par morceaux où ni A ni B ne
   change de tile ; un morceau impair laisse un pixel de gauche en
   attente, que le premier pixel du morceau suivant complète. Ce que
   l'assembleur gagne sur gcc :
   - tout l'état dans les registres (gcc range les curseurs dans la
     pile) ;
   - mot fusionné : le pixel de droite est lu par MOV.B @(1,Rm),R0, et
     seul lui passe par EXTU.B ; le pixel de gauche, décalé de 8 bits,
     garde son extension de signe au-dessus du bit 15, que MOV.W
     n'écrit pas.
   ========================================================================= */

#include "assets_types.h"

#if TILE_SHIFT != 4
#error "merge_sh2.S suppose des tiles de 16 x 16 (SHLL8 = x TILE_PIXELS)"
#endif

/* Champs de PlaneRow lus ici (compose.c vérifie ces positions) */
#define PR_ROW 0       /* numéros de tile de la rangée */
#define PR_ROW_END 4   /* juste après le dernier */
#define PR_TILE_LINE 8 /* ligne voulue dans la tile 0 */

/* Registres :
   r1  pixel suivant de A        r2  pixel suivant de B
   r3  pixels restants dans la tile de A
   r8  pixels restants dans la tile de B
   r9  case suivante de A        r10 case suivante de B
   r11 pixels restants à écrire  r12 pa          r13 pb
   r4  destination               r14 pixel en attente (0 : aucun ;
                                     sinon pixel | FFFFFF00h)
   r0, r5, r6, r7 : calculs, r7 compteur de mots */

        .text
        .align  2
        .global _merge_span_sh2
_merge_span_sh2:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        mov.l   r12,@-r15
        mov.l   r13,@-r15
        mov.l   r14,@-r15
        mov.l   @(28,r15),r0            /* xb (5e argument) */
        mov.l   @(32,r15),r11           /* n (6e argument) */
        mov     r5,r12
        mov     r7,r13

        /* Curseur de A à la colonne xa (r6) */
        mov     r6,r9
        shlr2   r9
        shlr2   r9
        add     r9,r9                   /* (xa >> 4) * 2 */
        mov.l   @(PR_ROW,r12),r5
        add     r5,r9                   /* case de xa */
        mov.w   @r9+,r5                 /* numéro de tile ; r9 : suivante */
        mov.l   @(PR_TILE_LINE,r12),r1
        extu.w  r5,r5
        shll8   r5                      /* x TILE_PIXELS */
        add     r5,r1
        mov     #15,r3
        and     r3,r6                   /* colonne dans la tile */
        add     r6,r1
        sub     r6,r3
        add     #1,r3                   /* 16 - colonne */
        mov.l   @(PR_ROW_END,r12),r5
        cmp/eq  r5,r9
        bf      1f
        mov.l   @(PR_ROW,r12),r9        /* le plan boucle */
1:
        /* Curseur de B à la colonne xb (r0) */
        mov     r0,r10
        shlr2   r10
        shlr2   r10
        add     r10,r10
        mov.l   @(PR_ROW,r13),r5
        add     r5,r10
        mov.w   @r10+,r5
        mov.l   @(PR_TILE_LINE,r13),r2
        extu.w  r5,r5
        shll8   r5
        add     r5,r2
        and     #15,r0
        add     r0,r2
        mov     #16,r8
        sub     r0,r8
        mov.l   @(PR_ROW_END,r13),r5
        cmp/eq  r5,r10
        bf      2f
        mov.l   @(PR_ROW,r13),r10
2:
        mov     #0,r14                  /* pas de pixel en attente */

        /* Morceau : min(reste de A, reste de B, reste à écrire) */
.Lpiece:
        mov     r3,r5
        cmp/hi  r8,r5
        bf      3f
        mov     r8,r5
3:      cmp/hi  r11,r5
        bf      4f
        mov     r11,r5
4:      sub     r5,r11
        sub     r5,r3
        tst     r14,r14
        bt.s    .Lpairs
        sub     r5,r8

        /* Un pixel en attente : le premier pixel du morceau complète son
           mot */
        mov.b   @r1+,r0
        mov.b   @r2+,r6
        tst     r0,r0
        bf      5f
        mov     r6,r0
5:      extu.b  r0,r0
        shll8   r14
        or      r14,r0
        mov.w   r0,@r4
        add     #2,r4
        mov     #0,r14
        add     #-1,r5

        /* Mots entiers : deux pixels à la fois. Les deux branchements ont
           leur créneau de retard rempli (BF/S : 2 cycles au lieu de 3 quand
           il est pris), et aucun registre n'est lu juste après son
           chargement (sinon 1 cycle d'attente) : les deux pixels de A sont
           lus avant le premier test, et l'avance de r2 sépare le chargement
           de B (gauche) de son décalage. */
.Lpairs:
        mov     r5,r7
        shlr    r7                      /* T : un pixel restera seul */
        movt    r6
        tst     r7,r7
        bt      .Ltail
.Lpair:
        mov.b   @(1,r1),r0              /* droite, A */
        mov.b   @r1,r5                  /* gauche, A */
        tst     r0,r0
        bf.s    6f
        add     #2,r1                   /* créneau : exécuté dans les deux cas */
        mov.b   @(1,r2),r0              /* droite, B */
6:      tst     r5,r5
        bf.s    7f
        extu.b  r0,r0                   /* créneau */
        mov.b   @r2,r5                  /* gauche, B */
7:      add     #2,r2
        shll8   r5
        or      r5,r0
        mov.w   r0,@r4
        dt      r7
        bf.s    .Lpair
        add     #2,r4

        /* Un pixel de gauche reste : il attend le morceau suivant */
.Ltail:
        tst     r6,r6
        bt      .Lnext
        mov.b   @r1+,r0
        mov.b   @r2+,r5
        tst     r0,r0
        bf      8f
        mov     r5,r0
8:      extu.b  r0,r14
        mov     #-1,r5
        shll8   r5
        or      r5,r14                  /* non nul : pixel en attente */

        /* Fin, ou tile suivante pour A et/ou B */
.Lnext:
        tst     r11,r11
        bt      .Ldone
        tst     r3,r3
        bf      9f
        mov.w   @r9+,r5
        mov.l   @(PR_TILE_LINE,r12),r1
        extu.w  r5,r5
        shll8   r5
        add     r5,r1
        mov.l   @(PR_ROW_END,r12),r5
        cmp/eq  r5,r9
        bf.s    9f
        mov     #16,r3
        mov.l   @(PR_ROW,r12),r9
9:      tst     r8,r8
        bf      .Lpiece
        mov.w   @r10+,r5
        mov.l   @(PR_TILE_LINE,r13),r2
        extu.w  r5,r5
        shll8   r5
        add     r5,r2
        mov.l   @(PR_ROW_END,r13),r5
        cmp/eq  r5,r10
        bf.s    .Lpiece
        mov     #16,r8
        bra     .Lpiece
        mov.l   @(PR_ROW,r13),r10

.Ldone:
        mov.l   @r15+,r14
        mov.l   @r15+,r13
        mov.l   @r15+,r12
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8
