/* =========================================================================
   Tests du moteur de course, sans le simulateur.

   1. Lignes : render_line contre un rendu naïf, pixel par pixel, sur des
      lignes de route et de décor quelconques (centre et largeur au
      hasard, en dehors de l'écran compris ; toutes les colonnes de départ
      du décor, paires et impaires). Chaque ligne doit écrire 160 mots
      (161 si elle est décalée d'un point), chacun une seule fois, et rien
      en dehors.
   2. Images : la trajectoire de la démo (le programme 68000, image après
      image, sur plus d'un tour) et des vues au hasard. Chaque CPU projette
      ses lignes (une sur deux) : elles doivent être exactement celles
      d'une projection de tout l'écran, et l'image dessinée par les deux
      CPU, identique au rendu naïf. La dernière ligne de l'écran doit
      toujours montrer la route (rien ne manque sous la voiture).
   3. Avec --ppm : l'image de référence d'un scénario du simulateur
      complet (tests/run_tests.sh).
      test_race --ppm IMAGES [--pad DEBUT-FIN:BOUTONS]... fichier.ppm
      Le programme 68000 tourne IMAGES fois avec la même manette que
      f1en32_headless ; l'image est dessinée avec les registres COMM
      obtenus. Le scénario doit mettre la démo en pause (Start) avant la
      capture : l'image capturée ne dépend plus alors du minutage.

   Le test fournit lui-même ce que le moteur et le 68000 attendent du
   simulateur : vdp_dram_write16 (dans un frame buffer de test, avec
   contrôle des écritures), les registres COMM et la manette.
   ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cost.h"
#include "framebuffer.h"
#include "game.h"
#include "interface.h"
#include "platform.h"
#include "project.h"
#include "race.h"
#include "render.h"
#include "shared.h"
#include "vdp.h"

#define SCREEN_HEIGHT 224

/* ---- Ce que fournirait le simulateur ------------------------------------- */

static uint8_t fb[VDP_DRAM_SIZE];
static uint8_t writtenWords[VDP_DRAM_SIZE / 2]; /* écritures par mot */
static unsigned long writes = 0;

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    if ((offset & 1) || offset + 2 > VDP_DRAM_SIZE) {
        fprintf(stderr, "ecriture 16 bits invalide en %05X\n",
                (unsigned)offset);
        exit(1);
    }
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
    writtenWords[offset / 2]++;
    writes++;
}

volatile uint16_t comm[COMM_COUNT];

#define MAX_PAD_STEPS 32
static struct {
    long first, last;
    unsigned buttons;
} padSteps[MAX_PAD_STEPS];
static int padCount = 0;
static long currentFrame = 0;

unsigned platform_read_pad(void) {
    unsigned pad = 0;
    for (int i = 0; i < padCount; i++)
        if (currentFrame >= padSteps[i].first &&
            currentFrame <= padSteps[i].last)
            pad |= padSteps[i].buttons;
    return pad;
}

/* ---- Rendu naïf ------------------------------------------------------------
   Écrit indépendamment de render.c, d'après la description de render.h :
   la couleur de chaque pixel, sans plages ni mots. */

static int refColumn(int32_t x) {
    long c = ((long)x + 32768) >> 16;
    return c < 0 ? 0 : (c > SCREEN_WIDTH ? SCREEN_WIDTH : (int)c);
}

static uint8_t refRoadPixel(const RoadLine *l, int px) {
    const int dark = l->flags & SEGMENT_DARK;
    const long w = l->w, r = w >> RUMBLE_SHIFT;
    const long lane = (long)(((long long)w * LANE_POS_MUL) >> 8);
    const long half = w >> LANE_HALF_SHIFT;
    const long x = l->x;

    if (px < refColumn(x - w - r) || px >= refColumn(x + w + r))
        return dark ? COLOR_GRASS_DARK : COLOR_GRASS_LIGHT;
    if (px < refColumn(x - w) || px >= refColumn(x + w))
        return dark ? COLOR_RUMBLE_DARK : COLOR_RUMBLE_LIGHT;
    if (!dark &&
        ((px >= refColumn(x - lane - half) &&
          px < refColumn(x - lane + half)) ||
         (px >= refColumn(x + lane - half) && px < refColumn(x + lane + half))))
        return COLOR_LANE;
    return dark ? COLOR_ROAD_DARK : COLOR_ROAD_LIGHT;
}

static uint8_t refSkyPixel(const Bitmap *sky, unsigned skyX, int y, int px) {
    if (y >= sky->height)
        return sky->pixels[(sky->height - 1) * sky->width];
    return sky
        ->pixels[y * sky->width + ((skyX + (unsigned)px) & (sky->width - 1))];
}

static uint8_t refPixel(const RoadLine *l, unsigned skyX, int y, int px) {
    return l->type == LINE_ROAD ? refRoadPixel(l, px)
                                : refSkyPixel(race_content.sky, skyX, y, px);
}

/* ---- Contrôles ------------------------------------------------------------
 */

static unsigned long failures = 0;

static void fail(const char *what, int y, int px, int got, int want) {
    if (failures++ < 20)
        fprintf(stderr, "ECHEC %s : ligne %d, pixel %d : %d au lieu de %d\n",
                what, y, px, got, want);
}

/* Dessine la ligne y avec render_line et vérifie écritures et pixels */
static int checkLine(const char *what, const RoadLine *l, unsigned skyX, int y,
                     RenderCost *cost) {
    const uint32_t offset = fb_offset(0, y);
    memset(writtenWords + offset / 2, 0, FB_PITCH / 2);
    const unsigned long before = writes;

    const int shift = render_line(offset, l, race_content.sky, skyX, y, cost);

    const unsigned words = SCREEN_WIDTH / 2 + (unsigned)shift;
    if (writes - before != words)
        fail(what, y, -1, (int)(writes - before), (int)words);
    for (unsigned i = 0; i < FB_PITCH / 2; i++)
        if (writtenWords[offset / 2 + i] != (i < words ? 1 : 0))
            fail(what, y, (int)(2 * i), writtenWords[offset / 2 + i],
                 i < words);

    for (int px = 0; px < SCREEN_WIDTH; px++) {
        const uint8_t got = fb[offset + (uint32_t)shift + (uint32_t)px];
        const uint8_t want = refPixel(l, skyX, y, px);
        if (got != want)
            fail(what, y, px, got, want);
    }
    return shift;
}

static uint32_t rng = 12345;
static uint32_t rnd(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}
static int32_t rndRange(int32_t min, int32_t max) {
    return min + (int32_t)(rnd() % (uint32_t)(max - min + 1));
}

/* 1. Lignes isolées */
static void testLines(void) {
    unsigned long lines = 0;
    RenderCost cost = {0};

    for (int i = 0; i < 200000; i++) {
        RoadLine l;
        l.type = LINE_ROAD;
        l.flags = (uint8_t)(rnd() & 1);
        /* demi-largeur : de 0 à 1 200 pixels ; centre : jusqu'à loin
           hors de l'écran, en 16.16 avec fraction */
        l.w = rndRange(0, 1200 << 16);
        l.x = rndRange(-1500 * 65536, 1820 * 65536);
        if (i % 4 == 0) /* route étroite près de l'écran */
            l.w = rndRange(0, 8 << 16);
        checkLine("route", &l, 0, (int)(rnd() % SCREEN_HEIGHT), &cost);
        lines++;
    }

    const Bitmap *sky = race_content.sky;
    for (unsigned skyX = 0; skyX < 2u * sky->width; skyX++)
        for (int y = 0; y < SCREEN_HEIGHT; y += 7) {
            RoadLine l = {0, 0, LINE_SKY, 0, 0};
            checkLine("decor", &l, skyX, y, &cost);
            lines++;
        }

    printf("lignes : %lu verifiees (%u plages)\n", lines, cost.runs);
}

/* 2. Images */
static RoadLine cpuLines[PLATFORM_CPU_COUNT][SCREEN_HEIGHT];
static RoadLine allLines[SCREEN_HEIGHT];
static uint64_t totalCycles[PLATFORM_CPU_COUNT];
static uint32_t worstCycles[PLATFORM_CPU_COUNT];
static unsigned long frames = 0;

static void checkFrame(const View *v) {
    const Track *t = race_content.track;

    project_lines(t, v, allLines, SCREEN_HEIGHT, 0, 1, &(ProjectCost){0});
    if (allLines[SCREEN_HEIGHT - 1].type != LINE_ROAD)
        fail("route sous la voiture", SCREEN_HEIGHT - 1, -1, 0, 1);

    const unsigned long before = writes;
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++) {
        ProjectCost pc = {0};
        RenderCost rc = {0};
        project_lines(t, v, cpuLines[cpu], SCREEN_HEIGHT, cpu,
                      PLATFORM_CPU_COUNT, &pc);
        for (int y = cpu; y < SCREEN_HEIGHT; y += PLATFORM_CPU_COUNT) {
            const RoadLine *a = &cpuLines[cpu][y], *b = &allLines[y];
            if (a->type != b->type || a->flags != b->flags ||
                (a->type == LINE_ROAD && (a->x != b->x || a->w != b->w)))
                fail("projection par CPU", y, -1, a->type, b->type);
            checkLine("image", a, v->skyX, y, &rc);
        }
        const uint32_t cycles = cost_cycles(&pc, &rc);
        totalCycles[cpu] += cycles;
        if (cycles > worstCycles[cpu])
            worstCycles[cpu] = cycles;
    }
    if (writes - before < SCREEN_WIDTH / 2 * SCREEN_HEIGHT ||
        writes - before > (SCREEN_WIDTH / 2 + 1) * SCREEN_HEIGHT)
        fail("ecritures par image", -1, -1, (int)(writes - before),
             SCREEN_WIDTH / 2 * SCREEN_HEIGHT);
    frames++;
}

static View viewFromComm(void) {
    View v;
    v.position =
        (uint32_t)comm[COMM_POSITION_HI] << 16 | comm[COMM_POSITION_LO];
    v.playerX = (int16_t)comm[COMM_PLAYER_X];
    v.skyX = comm[COMM_SKY_X];
    return v;
}

static void testFrames(void) {
    const Track *t = race_content.track;

    /* La démo : plus d'un tour à la vitesse de pointe */
    const long lap = (long)(track_length(t) / 192) + 600;
    for (currentFrame = 0; currentFrame < lap; currentFrame++) {
        md_vint();
        View v = viewFromComm();
        checkFrame(&v);
    }

    /* Vues quelconques, dont les deux bouts du circuit */
    for (int i = 0; i < 3000; i++) {
        View v;
        v.position = rnd() % track_length(t);
        if (i < 300)
            v.position = track_length(t) - 1 - (uint32_t)i * 37;
        v.playerX = (int16_t)rndRange(-1200, 1200);
        v.skyX = (uint16_t)rnd();
        checkFrame(&v);
    }

    printf("images : %lu verifiees\n", frames);
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        printf("  %s : modele (estime) %llu cycles en moyenne, %u au pire, "
               "hors ecritures FB\n",
               cpu ? "slave " : "master",
               (unsigned long long)(totalCycles[cpu] / frames),
               worstCycles[cpu]);
}

/* ---- 3. Image de référence d'un scénario ------------------------------- */

static uint32_t rgbOf(uint16_t color) {
    uint8_t r = color & 0x1F, g = (color >> 5) & 0x1F, b = (color >> 10) & 0x1F;
    r = (uint8_t)(r << 3 | r >> 2);
    g = (uint8_t)(g << 3 | g >> 2);
    b = (uint8_t)(b << 3 | b >> 2);
    return (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

static unsigned parseButtons(const char *s) {
    unsigned b = 0;
    for (; *s; s++)
        switch (*s) {
        case 'U':
            b |= PAD_UP;
            break;
        case 'D':
            b |= PAD_DOWN;
            break;
        case 'L':
            b |= PAD_LEFT;
            break;
        case 'R':
            b |= PAD_RIGHT;
            break;
        case 'A':
            b |= PAD_A;
            break;
        case 'B':
            b |= PAD_B;
            break;
        case 'C':
            b |= PAD_C;
            break;
        case 'S':
            b |= PAD_START;
            break;
        default:
            fprintf(stderr, "bouton inconnu : %c\n", *s);
            exit(1);
        }
    return b;
}

static int writeReference(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "usage : test_race --ppm IMAGES [--pad DEBUT-FIN:"
                        "BOUTONS]... fichier.ppm\n");
        return 1;
    }
    const long count = atol(argv[2]);
    int i = 3;
    for (; i + 1 < argc && strcmp(argv[i], "--pad") == 0; i += 2) {
        char buttons[16];
        if (padCount == MAX_PAD_STEPS ||
            sscanf(argv[i + 1], "%ld-%ld:%15s", &padSteps[padCount].first,
                   &padSteps[padCount].last, buttons) != 3)
            return 1;
        padSteps[padCount++].buttons = parseButtons(buttons);
    }
    if (i + 1 != argc)
        return 1;

    for (currentFrame = 0; currentFrame < count; currentFrame++)
        md_vint();
    const View v = viewFromComm();
    project_lines(race_content.track, &v, allLines, SCREEN_HEIGHT, 0, 1,
                  &(ProjectCost){0});

    uint16_t palette[PALETTE_COLORS];
    memcpy(palette, race_content.palette, sizeof palette);
    memcpy(palette + ROAD_COLOR_FIRST, road_palette, sizeof road_palette);

    FILE *f = fopen(argv[i], "wb");
    if (f == NULL) {
        perror(argv[i]);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            const uint32_t c =
                rgbOf(palette[refPixel(&allLines[y], v.skyX, y, px)]);
            const uint8_t rgb[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8),
                                    (uint8_t)c};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("position %u, ecart %d, decor %u\n", (unsigned)v.position, v.playerX,
           v.skyX);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--ppm") == 0)
        return writeReference(argc, argv);

    if ((race_content.sky->width & (race_content.sky->width - 1)) != 0) {
        fprintf(stderr, "ECHEC : largeur du decor pas puissance de 2\n");
        return 1;
    }

    testLines();
    testFrames();

    if (failures) {
        printf("%lu echecs\n", failures);
        return 1;
    }
    printf("0 echecs\n");
    return 0;
}
