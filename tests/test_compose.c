/* =========================================================================
   Tests de compose_line, hors simulateur (pas de threads, pas de VBlank).

   La référence est un rendu naïf, pixel par pixel, qui ne partage rien
   avec le moteur : pixel (x, y) de l'écran = pixel du plan A s'il est non
   nul, sinon pixel du plan B. Pour chaque ligne composée, on vérifie :
   - les 320 pixels, contre la référence ;
   - les écritures : 160 par ligne, toutes en 16 bits, à des adresses
     paires, chaque mot de la ligne écrit exactement une fois, rien hors
     de la ligne.

   Jeux d'essai :
   1. la trajectoire de la démo : xA de 0 à 448, xB synchronisé, toutes
      les lignes ;
   2. toutes les colonnes de départ de A (0 à 767), contre 40 valeurs de
      xB (les 32 premières, et des valeurs négatives ou au-delà du plan),
      sur des lignes et des défilements verticaux variés ;
   3. 300 000 lignes tirées au hasard (défilements de -3000 à 3000).

   Autre usage : écrire l'image de référence d'un écran entier, pour la
   comparer à une capture du simulateur (tests/run_tests.sh) :
     test_compose --ppm XA XB YA YB fichier.ppm
   ========================================================================= */

#include "compose.h"
#include "framebuffer.h"
#include "vdp.h"

#include "demo_assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_HEIGHT 224
#define LINE_WORDS (FB_WIDTH / 2)

static const Tileset *const tiles = &asset_tiles;
static const Tilemap *const planeA = &asset_plane_a;
static const Tilemap *const planeB = &asset_plane_b;

/* ---- Frame buffer de test -------------------------------------------------
   compose_line écrit par vdp_dram_write16 : on la fournit ici, à la place
   du simulateur, pour tout vérifier. Une seule ligne est composée à la
   fois, à l'offset LINE_OFFSET. */

#define LINE_OFFSET 0x1000u

static uint8_t fb[VDP_DRAM_SIZE];
static int wordWrites[LINE_WORDS]; /* écritures de chaque mot de la ligne */
static long badWrites = 0;         /* hors ligne ou mal alignées */
static long writeCount = 0;

void vdp_dram_write16(uint32_t offset, uint16_t pixels) {
    writeCount++;
    if ((offset & 1) || offset < LINE_OFFSET ||
        offset >= LINE_OFFSET + FB_WIDTH) {
        badWrites++;
        return;
    }
    fb[offset] = (uint8_t)(pixels >> 8);
    fb[offset + 1] = (uint8_t)pixels;
    wordWrites[(offset - LINE_OFFSET) / 2]++;
}

/* compose.c n'en a pas besoin, mais vdp.h les déclare */
void vdp_dram_write8(uint32_t offset, uint8_t pixel) {
    (void)offset;
    (void)pixel;
    badWrites++;
}

/* ---- Référence ------------------------------------------------------------
 */

static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

static uint8_t planePixel(const Tilemap *map, int x, int y) {
    x = wrap(x, map->widthTiles * TILE_SIZE);
    y = wrap(y, map->heightTiles * TILE_SIZE);
    const unsigned tile =
        map->cells[(y / TILE_SIZE) * map->widthTiles + x / TILE_SIZE];
    return tiles->pixels[tile * TILE_PIXELS + (y % TILE_SIZE) * TILE_SIZE +
                         x % TILE_SIZE];
}

static uint8_t referencePixel(int sx, int xA, int yA, int xB, int yB) {
    const uint8_t a = planePixel(planeA, xA + sx, yA);
    return a != 0 ? a : planePixel(planeB, xB + sx, yB);
}

/* ---- Vérification d'une ligne ---------------------------------------------
 */

static long linesChecked = 0;
static long failures = 0;
static ComposeCost cost;

static void checkLine(int xA, int yA, int xB, int yB) {
    memset(wordWrites, 0, sizeof wordWrites);
    memset(fb + LINE_OFFSET, 0xEE, FB_WIDTH);
    const long writesBefore = writeCount, badBefore = badWrites;

    compose_line(LINE_OFFSET, tiles, planeA, xA, yA, planeB, xB, yB, &cost);
    linesChecked++;

    int ok = writeCount - writesBefore == LINE_WORDS && badWrites == badBefore;
    for (int w = 0; w < LINE_WORDS; w++)
        ok &= wordWrites[w] == 1;

    int badX = -1;
    for (int sx = 0; sx < FB_WIDTH && badX < 0; sx++)
        if (fb[LINE_OFFSET + sx] != referencePixel(sx, xA, yA, xB, yB))
            badX = sx;

    if (!ok || badX >= 0) {
        if (failures < 10)
            fprintf(stderr,
                    "ECHEC xA=%d yA=%d xB=%d yB=%d : %ld ecritures, %ld "
                    "mauvaises, premier pixel faux x=%d\n",
                    xA, yA, xB, yB, writeCount - writesBefore,
                    badWrites - badBefore, badX);
        failures++;
    }
}

/* ---- Jeux d'essai ---------------------------------------------------------
 */

/* Générateur pseudo-aléatoire reproductible (xorshift) */
static uint32_t rngState = 2026;
static int randomIn(int min, int max) {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return min + (int)(rngState % (uint32_t)(max - min + 1));
}

static int demoScrollB(int xA) {
    const int maxA = planeA->widthTiles * TILE_SIZE - FB_WIDTH;
    const int maxB = planeB->widthTiles * TILE_SIZE - FB_WIDTH;
    return (xA * maxB + maxA / 2) / maxA;
}

static void testDemoPath(void) {
    const int maxA = planeA->widthTiles * TILE_SIZE - FB_WIDTH;
    const long before = linesChecked;
    memset(&cost, 0, sizeof cost);

    for (int xA = 0; xA <= maxA; xA++)
        for (int y = 0; y < SCREEN_HEIGHT; y++)
            checkLine(xA, y, demoScrollB(xA), y);

    const long frames = maxA + 1;
    printf("1. trajectoire de la demo : %ld lignes (%ld images)\n",
           linesChecked - before, frames);
    printf("   modele de couts, moyenne par image (deux CPU) : %u cycles hors "
           "ecritures FB\n",
           (unsigned)(cost_cycles(&cost) / frames));
    printf(
        "   par image : %u mots copies source paire, %u impaire, %u "
        "fusionnes ; %u morceaux de tile ; %u segments\n",
        (unsigned)(cost.wordsEven / frames), (unsigned)(cost.wordsOdd / frames),
        (unsigned)(cost.wordsMerged / frames),
        (unsigned)(cost.tileChunks / frames), (unsigned)(cost.runs / frames));
}

static void testAllColumns(void) {
    const int widthA = planeA->widthTiles * TILE_SIZE;
    const int widthB = planeB->widthTiles * TILE_SIZE;
    int xBs[40];
    for (int i = 0; i < 32; i++)
        xBs[i] = i;
    xBs[32] = -1;
    xBs[33] = -17;
    xBs[34] = widthB - 1;
    xBs[35] = widthB - 16;
    xBs[36] = widthB + 5;
    xBs[37] = 3 * widthB + 333;
    xBs[38] = -widthB - 7;
    xBs[39] = 351;

    const long before = linesChecked;
    for (int xA = 0; xA < widthA; xA++)
        for (int i = 0; i < 40; i++)
            for (int y = 0; y < SCREEN_HEIGHT; y += 9) {
                const int yA = y + randomIn(-300, 300);
                const int yB = y + randomIn(-300, 300);
                checkLine(xA, yA, xBs[i], yB);
            }
    printf("2. toutes les colonnes de A x 40 valeurs de xB : %ld lignes\n",
           linesChecked - before);
}

static void testRandom(void) {
    const long before = linesChecked;
    for (int i = 0; i < 300000; i++)
        checkLine(randomIn(-3000, 3000), randomIn(-3000, 3000),
                  randomIn(-3000, 3000), randomIn(-3000, 3000));
    printf("3. lignes au hasard : %ld lignes\n", linesChecked - before);
}

/* ---- Image de référence ---------------------------------------------------
 */

/* Même conversion que le simulateur (pixel/vdp.c) */
static void paletteRGB(uint8_t index, uint8_t rgb[3]) {
    const uint16_t c = asset_palette[index];
    const uint8_t r = c & 0x1F, g = (c >> 5) & 0x1F, b = (c >> 10) & 0x1F;
    rgb[0] = (uint8_t)((r << 3) | (r >> 2));
    rgb[1] = (uint8_t)((g << 3) | (g >> 2));
    rgb[2] = (uint8_t)((b << 3) | (b >> 2));
}

static int writeReferencePPM(int xA, int xB, int yA, int yB, const char *path) {
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", FB_WIDTH, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            uint8_t rgb[3];
            paletteRGB(referencePixel(x, xA, yA + y, xB, yB + y), rgb);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 7 && strcmp(argv[1], "--ppm") == 0)
        return writeReferencePPM(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]),
                                 atoi(argv[5]), argv[6]);
    if (argc != 1) {
        fprintf(stderr,
                "usage : test_compose [--ppm XA XB YA YB fichier.ppm]\n");
        return 2;
    }

    testDemoPath();
    testAllColumns();
    testRandom();

    printf("%ld lignes verifiees, %ld echecs, %ld ecritures (toutes 16 bits)\n",
           linesChecked, failures, writeCount);
    return failures != 0 || badWrites != 0;
}
