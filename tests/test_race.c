/* =========================================================================
   Tests du moteur de course, sans le simulateur.

   Le test prépare les lignes (render_prepare) dans un frame buffer de
   test, puis, pour chaque ligne d'écran, fait ce que fera le VDP : lire
   l'entrée de la table de lignes, appliquer le décalage d'écran (SFT) et
   prendre les couleurs claires ou sombres de la route. Il compare ce qui
   serait affiché à un rendu naïf, écrit indépendamment du moteur d'après
   la description de render.h : la route de demi-largeur w_k la plus
   proche, centrée sur le pixel le plus proche.

   1. Lignes : des lignes de route quelconques (centre et largeur au
      hasard, loin hors de l'écran compris) et toutes les lignes de ciel.
      Chaque ligne doit écrire un seul mot, son entrée de la table, et la
      fenêtre affichée doit rester dans les lignes préparées.
   2. Images : la trajectoire de la démo (le programme 68000, image après
      image, sur plus d'un tour) et des vues au hasard. Chaque CPU projette
      ses lignes (une sur deux) : elles doivent être exactement celles
      d'une projection de tout l'écran, et l'image, identique au rendu
      naïf ; 224 écritures par image. La dernière ligne de l'écran doit
      toujours montrer la route (rien ne manque sous la voiture).
   3. Avec --ppm : l'image de référence d'un scénario du simulateur
      complet (tests/run_tests.sh).
      test_race --ppm IMAGES [--pad DEBUT-FIN:BOUTONS]... fichier.ppm
      Le programme 68000 tourne IMAGES fois avec la même manette que
      f1en32_headless ; l'image est calculée par le rendu naïf avec les
      registres COMM obtenus. Le scénario doit mettre la démo en pause
      (Start) avant la capture : l'image capturée ne dépend plus alors du
      minutage.

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
static unsigned long writes = 0;
static uint32_t lastWrite = 0;

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    if ((offset & 1) || offset + 2 > VDP_DRAM_SIZE) {
        fprintf(stderr, "ecriture 16 bits invalide en %05X\n",
                (unsigned)offset);
        exit(1);
    }
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
    lastWrite = offset;
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
   l'index de couleur de chaque pixel, et la bande (claire ou sombre). */

typedef struct {
    uint8_t index[SCREEN_WIDTH];
    int dark; /* couleurs sombres de la route ; sans objet pour le ciel */
    int road;
} ShownLine;

static long refColumn(long v) { return (v + 32768) >> 16; }

static uint8_t refRoadIndex(long w, long d) {
    const long r = w >> RUMBLE_SHIFT;
    const long lane = (long)(((long long)w * LANE_POS_MUL) >> 8);
    const long half = w >> LANE_HALF_SHIFT;
    if (d < refColumn(-w - r) || d >= refColumn(w + r))
        return COLOR_GRASS;
    if (d < refColumn(-w) || d >= refColumn(w))
        return COLOR_RUMBLE;
    if ((d >= refColumn(-lane - half) && d < refColumn(-lane + half)) ||
        (d >= refColumn(lane - half) && d < refColumn(lane + half)))
        return COLOR_LANE;
    return COLOR_ROAD;
}

static void refLine(const RoadLine *l, int y, ShownLine *out) {
    if (l->type == LINE_ROAD) {
        /* La demi-largeur préparée la plus proche, le pixel le plus proche */
        unsigned long long k =
            ((unsigned long long)(uint32_t)l->w * ROAD_WIDTH_INV +
             0x80000000ull) >>
            32;
        if (k > ROAD_WIDTHS - 1)
            k = ROAD_WIDTHS - 1;
        const long w = (long)k * ROAD_WIDTH_STEP;
        const long center = refColumn(l->x);
        for (int px = 0; px < SCREEN_WIDTH; px++)
            out->index[px] = refRoadIndex(w, px - center);
        out->dark = (l->flags & SEGMENT_DARK) != 0;
        out->road = 1;
    } else {
        const int band = y >> SKY_BAND_SHIFT;
        const uint8_t c =
            (uint8_t)(band < SKY_BANDS ? COLOR_SKY_FIRST + band : COLOR_GROUND);
        memset(out->index, c, SCREEN_WIDTH);
        out->dark = 0;
        out->road = 0;
    }
}

/* ---- Ce qu'afficherait le VDP ----------------------------------------------
 */

static RoadLayout layout;
static unsigned long failures = 0;

static void fail(const char *what, int y, int px, long got, long want) {
    if (failures++ < 20)
        fprintf(stderr, "ECHEC %s : ligne %d, pixel %d : %ld au lieu de %ld\n",
                what, y, px, got, want);
}

/* Choisit la ligne y avec render_line, puis la lit comme le VDP */
static void shownLine(const char *what, const RoadLine *l, int y,
                      RenderCost *cost, ShownLine *out) {
    const unsigned long before = writes;
    const unsigned a = render_line(y, l, &layout, cost);
    if (writes - before != 1 || lastWrite != (uint32_t)y * 2)
        fail(what, y, -1, (long)(writes - before), 1);

    const uint32_t word = (uint32_t)fb[y * 2] << 8 | fb[y * 2 + 1];
    const uint32_t start = word * 2 + (a & LINE_SFT);
    if (start < FB_LINE_TABLE_BYTES || start + SCREEN_WIDTH > layout.end)
        fail("fenetre hors des lignes preparees", y, -1, (long)start, 0);
    else
        memcpy(out->index, fb + start, SCREEN_WIDTH);
    out->dark = (a & LINE_DARK) != 0;
    out->road = l->type == LINE_ROAD;
}

static void compare(const char *what, const RoadLine *l, int y,
                    RenderCost *cost) {
    ShownLine got, want;
    shownLine(what, l, y, cost, &got);
    refLine(l, y, &want);
    for (int px = 0; px < SCREEN_WIDTH; px++)
        if (got.index[px] != want.index[px]) {
            fail(what, y, px, got.index[px], want.index[px]);
            break;
        }
    if (want.road && got.dark != want.dark)
        fail("bande claire ou sombre", y, -1, got.dark, want.dark);
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

    for (int i = 0; i < 300000; i++) {
        RoadLine l;
        l.type = LINE_ROAD;
        l.flags = (uint8_t)(rnd() & 1);
        /* demi-largeur : de 0 à 200 pixels (au-delà de la plus large
           préparée) ; centre : jusqu'à loin hors de l'écran */
        l.w = rndRange(0, 200 * 65536);
        if (i % 4 == 0) /* route étroite, près de l'horizon */
            l.w = rndRange(0, 8 * 65536);
        l.x = rndRange(-1500 * 65536, 1820 * 65536);
        if (i % 3 == 0) /* centre près de l'écran */
            l.x = rndRange(-300 * 65536, 620 * 65536);
        compare("route", &l, (int)(rnd() % SCREEN_HEIGHT), &cost);
        lines++;
    }
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        RoadLine l = {0, 0, LINE_SKY, 0, 0};
        compare("ciel", &l, y, &cost);
        lines++;
    }
    printf("lignes : %lu verifiees\n", lines);
}

/* 2. Images */
static RoadLine cpuLines[PLATFORM_CPU_COUNT][SCREEN_HEIGHT];
static RoadLine allLines[SCREEN_HEIGHT];
static uint64_t totalCycles[PLATFORM_CPU_COUNT];
static uint32_t worstCycles[PLATFORM_CPU_COUNT];
static unsigned long frames = 0, darkChanges = 0;

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
            compare("image", a, y, &rc);
        }
        const uint32_t cycles = cost_cycles(&pc, &rc);
        totalCycles[cpu] += cycles;
        if (cycles > worstCycles[cpu])
            worstCycles[cpu] = cycles;
    }
    if (writes - before != SCREEN_HEIGHT)
        fail("ecritures par image", -1, -1, (long)(writes - before),
             SCREEN_HEIGHT);

    /* Changements de couleurs que fera le Master (statistique) */
    int dark = -1;
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        if (allLines[y].type == LINE_ROAD) {
            const int d = allLines[y].flags & SEGMENT_DARK;
            if (d != dark)
                darkChanges++;
            dark = d;
        }
    frames++;
}

static View viewFromComm(void) {
    View v;
    v.position =
        (uint32_t)comm[COMM_POSITION_HI] << 16 | comm[COMM_POSITION_LO];
    v.playerX = (int16_t)comm[COMM_PLAYER_X];
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
        checkFrame(&v);
    }

    printf("images : %lu verifiees, %.1f changements de couleurs de la route "
           "par image en moyenne\n",
           frames, (double)darkChanges / frames);
    for (int cpu = 0; cpu < PLATFORM_CPU_COUNT; cpu++)
        printf("  %s : modele %llu cycles en moyenne, %u au pire, "
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

static uint16_t colorOf(uint8_t index, int dark) {
    if (index >= COLOR_ROAD_FIRST && index < ROAD_COLOR_END)
        return (dark ? road_dark : road_light)[index - COLOR_ROAD_FIRST];
    if (index >= COLOR_SKY_FIRST && index <= COLOR_GROUND)
        return sky_palette[index - COLOR_SKY_FIRST];
    return 0;
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

    FILE *f = fopen(argv[i], "wb");
    if (f == NULL) {
        perror(argv[i]);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        ShownLine l;
        refLine(&allLines[y], y, &l);
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            const uint32_t c = rgbOf(colorOf(l.index[px], l.dark));
            const uint8_t rgb[3] = {(uint8_t)(c >> 16), (uint8_t)(c >> 8),
                                    (uint8_t)c};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("position %u, ecart %d\n", (unsigned)v.position, v.playerX);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--ppm") == 0)
        return writeReference(argc, argv);

    render_layout(&layout);
    if (!render_prepare(&layout)) {
        printf("ECHEC : les lignes preparees ne tiennent pas (%u octets)\n",
               (unsigned)layout.end);
        return 1;
    }
    printf("lignes preparees : %u octets sur %u (%u libres), %lu ecritures "
           "au demarrage\n",
           (unsigned)layout.end, VDP_DRAM_SIZE,
           (unsigned)(VDP_DRAM_SIZE - layout.end), writes);

    testLines();
    testFrames();

    if (failures) {
        printf("%lu echecs\n", failures);
        return 1;
    }
    printf("0 echecs\n");
    return 0;
}
