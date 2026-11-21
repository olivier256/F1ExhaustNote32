#ifndef BENCH_FRAMES_H
#define BENCH_FRAMES_H

/* =========================================================================
   Images mesurées par le banc sh2cycles : la même liste pour le programme
   SH2 (bench.c, temps mesuré) et pour le programme hôte (costs.c,
   compteurs du modèle). Reproductible : pas de hasard, un générateur fixe.
   - 0 à 89   : la trajectoire de la démo (xA de 0 à 448, pairs et
                impairs, xB = xA x 11 / 14) ;
   - 90 à 179 : défilements quelconques, pour décorréler les parités de
                A et B et varier les lignes de tile mixtes.
   ========================================================================= */

#define BENCH_FRAMES 180
#define BENCH_SCREEN_HEIGHT 224

typedef struct {
    int xA, xB, yA, yB;
} BenchFrame;

static inline BenchFrame bench_frame(int i) {
    BenchFrame f;
    if (i < 90) {
        f.xA = (i * 448) / 89 + (i & 1); /* pairs et impairs */
        if (f.xA > 448)
            f.xA = 448;
        f.xB = (f.xA * 352 + 224) / 448;
        f.yA = f.yB = 0;
    } else {
        unsigned s = 2026u + (unsigned)i * 2654435761u;
        s ^= s >> 13;
        s *= 0x5bd1e995u;
        s ^= s >> 15;
        f.xA = (int)(s % 2001u) - 1000;
        f.xB = (int)((s >> 11) % 2001u) - 1000;
        f.yA = (int)((s >> 3) % 449u) - 224;
        f.yB = (int)((s >> 17) % 449u) - 224;
    }
    return f;
}

#endif
