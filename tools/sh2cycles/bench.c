/* =========================================================================
   Banc sh2cycles, côté SH2 : pour chaque image de frames.h, projette le
   circuit et écrit la table de lignes pour les lignes d'un CPU, avec le
   vrai code de race/project.c, race/render.c et race/hud.c (Master)
   compilé pour SH2 (sans les
   compteurs du modèle), et marque chaque image pour le greffon qemu qui
   mesure le temps (sh2cycles_plugin.c).

   BENCH_FIRST_LINE : 0 pour le Master (lignes paires), 1 pour le Slave.
   Chaque CPU ayant son propre cache, on lance un programme par CPU.

   Les lignes préparées sont écrites une fois, hors mesure. Après chaque
   image, hors mesure aussi, une empreinte de ce que le VDP afficherait
   (table de lignes, SFT, couleurs claires ou sombres) est imprimée : elle
   doit être identique à celle du programme hôte (costs.c), preuve que le
   code SH2 calcule juste.
   ========================================================================= */

#include "framebuffer.h"
#include "hud.h"
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

/* Description et attributs de chaque ligne, comme dans race.c */
static RoadLayout layout;
static RoadLine lines[BENCH_SCREEN_HEIGHT];
static uint8_t attributes[BENCH_SCREEN_HEIGHT];

static void drawFrame(const View *v) {
    project_lines(race_content.track, v, lines, BENCH_SCREEN_HEIGHT,
                  BENCH_FIRST_LINE, 2, 0);
    for (int y = BENCH_FIRST_LINE; y < BENCH_SCREEN_HEIGHT; y += 2)
        attributes[y] = (uint8_t)render_line(y, &lines[y], &layout, 0);
    if (BENCH_FIRST_LINE == 0) /* le compteur : Master seulement */
        hud_build(v->position);
}

/* Empreinte de ce que le VDP afficherait : pour chaque ligne, les 320
   octets montrés (table de lignes et SFT) et la bande claire ou sombre */
static uint32_t fingerprint(void) {
    uint32_t h = 2166136261u;
    for (int y = BENCH_FIRST_LINE; y < BENCH_SCREEN_HEIGHT; y += 2) {
        const uint32_t word =
            (uint32_t)bench_fb[2 * y] << 8 | bench_fb[2 * y + 1];
        const uint32_t start = word * 2 + (attributes[y] & LINE_SFT);
        for (int x = 0; x < FB_WIDTH; x++)
            h = (h ^ bench_fb[start + x]) * 16777619u;
        h = (h ^ (attributes[y] & LINE_DARK)) * 16777619u;
    }
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

    render_layout(&layout);
    if (!render_prepare(&layout))
        return 1;

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
