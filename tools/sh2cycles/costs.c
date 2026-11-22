/* =========================================================================
   Banc sh2cycles, côté hôte : pour chaque image de frames.h et les lignes
   d'un CPU, les compteurs du modèle de coûts (race/cost.h) et l'empreinte
   des lignes dessinées.

   Usage : costs PREMIERE_LIGNE      (0 = Master, 1 = Slave)
   Sortie (stdout), une ligne par image :
     image;segments;roadLines;skyLines;runs;skyChunks;modele;empreinte;
     lignes_decalees
   ========================================================================= */

#include "framebuffer.h"
#include "project.h"
#include "race.h"
#include "render.h"
#include "vdp.h"

#include "frames.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t fb[VDP_DRAM_SIZE];

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage : costs PREMIERE_LIGNE\n");
        return 2;
    }
    const int first = atoi(argv[1]);
    const RaceContent *c = &race_content;
    static RoadLine lines[BENCH_SCREEN_HEIGHT];

    for (int i = 0; i < BENCH_FRAMES; i++) {
        const View v = bench_frame(c->track, i);
        ProjectCost p = {0};
        RenderCost r = {0};
        int shift[BENCH_SCREEN_HEIGHT] = {0}, shifted = 0;
        project_lines(c->track, &v, lines, BENCH_SCREEN_HEIGHT, first, 2, &p);
        for (int y = first; y < BENCH_SCREEN_HEIGHT; y += 2) {
            shift[y] =
                render_line(fb_offset(0, y), &lines[y], c->sky, v.skyX, y, &r);
            shifted += shift[y];
        }

        /* Empreinte de ce que l'écran affiche (voir bench.c) */
        uint32_t h = 2166136261u;
        for (int y = first; y < BENCH_SCREEN_HEIGHT; y += 2)
            for (int x = 0; x < FB_WIDTH; x++)
                h = (h ^ fb[fb_offset(x + shift[y], y)]) * 16777619u;

        printf("%d;%u;%u;%u;%u;%u;%u;%08x;%d\n", i, p.segments, r.roadLines,
               r.skyLines, r.runs, r.skyChunks, cost_cycles(&p, &r), h,
               shifted);
    }
    return 0;
}
