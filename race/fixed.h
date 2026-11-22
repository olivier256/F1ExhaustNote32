#ifndef FIXED_H
#define FIXED_H

#include <stdint.h>

/* =========================================================================
   Calcul en virgule fixe.

   Le SH2 n'a pas d'instruction de division : en C, « / » appelle une
   routine de libgcc (__udivsi3, une soixantaine de cycles avec DIV1).
   Toutes les divisions du moteur passent par div_u32, pour pouvoir la
   remplacer d'un coup par l'unité de division du SH7604 (DIVU, environ
   40 cycles, et le CPU peut travailler pendant ce temps) si le banc
   tools/sh2cycles montre qu'elle compte. Elles sont rares : une par
   segment projeté, une par segment dessiné.

   Les produits 32 x 32 -> 64 bits (mul64) donnent l'instruction DMULS.L
   du SH2 (2 à 4 cycles).
   ========================================================================= */

static inline uint32_t div_u32(uint32_t a, uint32_t b) { return a / b; }

static inline int64_t mul64(int32_t a, int32_t b) {
    return (int64_t)a * (int64_t)b;
}

/* Ramène un résultat 64 bits dans [-2^30, 2^30] : des coordonnées d'écran
   hors de cet intervalle (16.16 : 16 384 pixels) ne sont jamais vues. */
#define FIXED_LIMIT (1 << 30)
static inline int32_t clamp30(int64_t v) {
    if (v > FIXED_LIMIT)
        return FIXED_LIMIT;
    if (v < -FIXED_LIMIT)
        return -FIXED_LIMIT;
    return (int32_t)v;
}

/* Partie entière par excès d'un 16.16 (décalage arithmétique : gcc et le
   SH2 décalent les entiers signés en gardant le signe) */
static inline int ceil16(int32_t v) { return (v + 0xFFFF) >> 16; }

/* Code froid : ce qui ne sert pas à chaque image (la préparation des
   lignes au démarrage, les fonctions des tests). Sur SH2, il est rangé à
   part (section .text.sdram) : l'édition de liens le laisse en SDRAM, et
   réserve la RAM interne au code de chaque image (voir
   tools/sh2cycles/bench_ram.ld). */
#if defined(__sh__)
#define COLD_CODE __attribute__((noinline, section(".text.sdram")))
#else
#define COLD_CODE __attribute__((noinline))
#endif

#endif
