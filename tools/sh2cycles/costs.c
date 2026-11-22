/* =========================================================================
   Banc sh2cycles, côté hôte : pour chaque image de frames.h et les lignes
   d'un CPU, les compteurs du modèle de coûts (race/cost.h) et l'empreinte
   de ce que le VDP afficherait (voir bench.c).

   Usage : costs PREMIERE_LIGNE      (0 = Master, 1 = Slave)
   Sortie (stdout), une ligne par image :
     image;segments;roadLines;skyLines;hud;modele;empreinte;ecritures_FB
   ========================================================================= */

#include "framebuffer.h"
#include "hud.h"
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
    static RoadLayout layout;
    static RoadLine lines[BENCH_SCREEN_HEIGHT];

    render_layout(&layout);
    if (!render_prepare(&layout))
        return 1;

    for (int i = 0; i < BENCH_FRAMES; i++) {
        const View v = bench_frame(c->track, i);
        ProjectCost p = {0};
        RenderCost r = {0};
        unsigned attributes[BENCH_SCREEN_HEIGHT] = {0};
        project_lines(c->track, &v, lines, BENCH_SCREEN_HEIGHT, first, 2, &p);
        for (int y = first; y < BENCH_SCREEN_HEIGHT; y += 2)
            attributes[y] = render_line(y, &lines[y], &layout, &r);
        const int hud = first == 0;
        uint32_t fbWrites = BENCH_SCREEN_HEIGHT / 2;
        if (hud)
            fbWrites += 2 + hud_build(v.position) / 2;

        /* Empreinte de ce que le VDP afficherait (voir bench.c) */
        uint32_t h = 2166136261u;
        for (int y = first; y < BENCH_SCREEN_HEIGHT; y += 2) {
            const uint32_t word = (uint32_t)fb[2 * y] << 8 | fb[2 * y + 1];
            const uint32_t start = word * 2 + (attributes[y] & LINE_SFT);
            for (int x = 0; x < FB_WIDTH; x++)
                h = (h ^ fb[start + x]) * 16777619u;
            h = (h ^ (attributes[y] & LINE_DARK)) * 16777619u;
        }

        printf("%d;%u;%u;%u;%d;%u;%08x;%u\n", i, p.segments, r.roadLines,
               r.skyLines, hud, cost_cycles(&p, &r) + (hud ? CYCLES_HUD : 0), h,
               fbWrites);
    }
    return 0;
}
