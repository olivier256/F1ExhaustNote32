/* =========================================================================
   Banc sh2cycles, côté hôte : pour chaque image de frames.h et les lignes
   d'un CPU, les compteurs du modèle de coûts (supervdp/cost.h) et
   l'empreinte des lignes dessinées.

   Usage : costs PREMIERE_LIGNE      (0 = Master, 1 = Slave)
   Sortie (stdout), une ligne par image :
     image;lines;aTiles;runs;tileChunks;wordsEven;wordsOdd;wordsMerged;modele;empreinte
   ========================================================================= */

#include "compose.h"
#include "framebuffer.h"
#include "vdp.h"

#include "demo_assets.h"
#include "frames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t fb[VDP_DRAM_SIZE];

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
}

void vdp_dram_write8(uint32_t offset, uint8_t pixel) { fb[offset] = pixel; }

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage : costs PREMIERE_LIGNE\n");
        return 2;
    }
    const int first = atoi(argv[1]);

    for (int i = 0; i < BENCH_FRAMES; i++) {
        const BenchFrame f = bench_frame(i);
        ComposeCost c;
        memset(&c, 0, sizeof c);
        for (int y = first; y < BENCH_SCREEN_HEIGHT; y += 2)
            compose_line(fb_offset(0, y), &asset_tiles, &asset_plane_a, f.xA,
                         f.yA + y, &asset_plane_b, f.xB, f.yB + y, &c);

        uint32_t h = 2166136261u;
        for (int y = first; y < BENCH_SCREEN_HEIGHT; y += 2)
            for (int x = 0; x < FB_WIDTH; x++)
                h = (h ^ fb[fb_offset(x, y)]) * 16777619u;

        printf("%d;%u;%u;%u;%u;%u;%u;%u;%u;%08x\n", i, c.lines, c.aTiles,
               c.runs, c.tileChunks, c.wordsEven, c.wordsOdd, c.wordsMerged,
               cost_cycles(&c), h);
    }
    return 0;
}
