#ifndef BENCH_FRAMES_H
#define BENCH_FRAMES_H

/* =========================================================================
   Images mesurées par le banc sh2cycles : la même liste pour le programme
   SH2 (bench.c, temps mesuré) et pour le programme hôte (costs.c,
   compteurs du modèle). Reproductible : pas de hasard, un générateur fixe.
   Les positions parcourent tout le circuit de la démo (virages, crêtes,
   descentes, et le passage de la fin au début) ; l'écart latéral varie
   d'une image à l'autre.
   ========================================================================= */

#include "project.h"

#define BENCH_FRAMES 180
#define BENCH_SCREEN_HEIGHT 224

static inline View bench_frame(const Track *t, int i) {
    View v;
    unsigned s = 2026u + (unsigned)i * 2654435761u;
    s ^= s >> 13;
    s *= 0x5bd1e995u;
    s ^= s >> 15;
    /* Hors mesure : la division (libgcc sur SH2) ne compte pas */
    v.position =
        (uint32_t)((uint64_t)track_length(t) * (unsigned)i / BENCH_FRAMES);
    v.position += s % SEGMENT_LENGTH;
    if (v.position >= track_length(t))
        v.position -= track_length(t);
    v.playerX = (int16_t)((int)(s >> 8) % 1201 - 600);
    return v;
}

#endif
