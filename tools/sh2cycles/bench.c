/* =========================================================================
   Banc sh2cycles, côté SH2 : dessine les lignes d'un CPU pour chaque image
   de frames.h, avec le vrai code de race/project.c et race/render.c
   compilé pour SH2 (sans les compteurs du modèle), et marque chaque image
   pour le greffon qemu qui mesure le temps (sh2cycles_plugin.c).

   BENCH_FIRST_LINE : 0 pour le Master (lignes paires), 1 pour le Slave.
   Chaque CPU ayant son propre cache, on lance un programme par CPU.

   Après chaque image, hors mesure, une empreinte des lignes dessinées est
   imprimée : elle doit être identique à celle du programme hôte
   (costs.c), preuve que le code SH2 dessine juste.
   ========================================================================= */

#include "framebuffer.h"
#include "project.h"
#include "race.h"
#include "render.h"
#include "vdp.h"

#include "frames.h"

#ifndef BENCH_FIRST_LINE
#define BENCH_FIRST_LINE 0
#endif

/* Placés par bench.ld : 2000 0000h (marqueurs), 2400 0000h (frame buffer) */
volatile uint32_t bench_marker[4] __attribute__((section(".marker")));
volatile uint8_t bench_fb[VDP_DRAM_SIZE] __attribute__((section(".fb")));

void sys_write(const char *text, int length);

/* Description et décalage d'écran de chaque ligne, comme dans race.c */
static RoadLine lines[BENCH_SCREEN_HEIGHT];
static uint8_t lineShift[BENCH_SCREEN_HEIGHT];

static void drawFrame(const View *v) {
    const RaceContent *c = &race_content;
    project_lines(c->track, v, lines, BENCH_SCREEN_HEIGHT, BENCH_FIRST_LINE, 2,
                  0);
    for (int y = BENCH_FIRST_LINE; y < BENCH_SCREEN_HEIGHT; y += 2)
        lineShift[y] = (uint8_t)render_line(fb_offset(0, y), &lines[y], c->sky,
                                            v->skyX, y, 0);
}

/* Empreinte de ce que l'écran affiche : 320 pixels par ligne, à partir
   du pixel 'décalage' de la ligne */
static uint32_t fingerprint(void) {
    uint32_t h = 2166136261u;
    for (int y = BENCH_FIRST_LINE; y < BENCH_SCREEN_HEIGHT; y += 2)
        for (int x = 0; x < FB_WIDTH; x++)
            h = (h ^ bench_fb[fb_offset(x + lineShift[y], y)]) * 16777619u;
    return h;
}

static void printHex(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    char text[9];
    for (int i = 0; i < 8; i++)
        text[i] = digits[(v >> (28 - 4 * i)) & 15];
    text[8] = '\n';
    sys_write(text, 9);
}

int main(void) {
    const Track *t = race_content.track;

    /* Une image hors mesure, pour remplir le cache comme en régime établi */
    View warm = bench_frame(t, 0);
    drawFrame(&warm);

    for (int i = 0; i < BENCH_FRAMES; i++) {
        const View v = bench_frame(t, i);
        bench_marker[0] = 1; /* début de la mesure */
        drawFrame(&v);
        bench_marker[1] = 1; /* fin */
        printHex(fingerprint());
    }
    return 0;
}
