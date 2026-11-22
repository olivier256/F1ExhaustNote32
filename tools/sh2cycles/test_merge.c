/* =========================================================================
   Banc sh2cycles : vérifie la fusion en assembleur (merge_sh2.S) contre
   la fusion en C (mergeSpan, compose.c), sur SH2 sous qemu.

   compose.c est inclus ici, compilé avec SUPERVDP_ASM=1 : les deux
   versions sont visibles. Pour chaque cas, les deux écrivent dans leur
   tampon, entouré de mots de garde ; les tampons doivent être identiques,
   gardes comprises.
   - toutes les colonnes de A x toutes les colonnes de B modulo 32, sur
     des longueurs de 2 à 40 pixels, à une ligne de tile mixte ;
   - 100 000 cas au hasard (générateur fixe) : lignes, colonnes, et
     longueurs de 2 à 322 pixels.
   Code de retour : nombre de cas faux (0 : tout est bon).
   ========================================================================= */

#define SUPERVDP_ASM 1
#include "compose.c"
#include "demo_assets.h"

void sys_write(const char *text, int length);

/* Segments attendus par bench_ram.ld (inutilisés ici) */
volatile uint32_t bench_marker[4] __attribute__((section(".marker")));
volatile uint8_t bench_fb[16] __attribute__((section(".fb")));

#define GUARD 4
#define WORDS (LINE_SPAN / 2 + 2 * GUARD)

static uint16_t bufC[WORDS], bufAsm[WORDS];
static uint32_t seed = 12345;

static uint32_t nextRandom(void) {
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

/* Reste de la division, sans l'instruction de division du SH2 absente */
static unsigned modulo(uint32_t v, unsigned n) {
    while (v >= n)
        v -= n;
    return v;
}

static void printNumber(const char *label, uint32_t v) {
    char text[40];
    int k = 0;
    while (*label)
        text[k++] = *label++;
    char digits[12];
    int d = 0;
    do {
        uint32_t q = 0, r = v;
        while (r >= 10) { /* v / 10 sans division */
            r -= 10;
            q++;
        }
        digits[d++] = (char)('0' + r);
        v = q;
    } while (v != 0);
    while (d > 0)
        text[k++] = digits[--d];
    text[k++] = '\n';
    sys_write(text, k);
}

static int oneCase(int yA, int yB, unsigned xa, unsigned xb, unsigned n) {
    PlaneRow pa, pb;
    planeRowInit(&pa, &asset_plane_a, &asset_tiles, yA);
    planeRowInit(&pb, &asset_plane_b, &asset_tiles, yB);
    for (int i = 0; i < WORDS; i++)
        bufC[i] = bufAsm[i] = (uint16_t)(0xA5A5 ^ i);
    mergeSpan(bufC + GUARD, &pa, xa, &pb, xb, n, 0);
    merge_span_sh2(bufAsm + GUARD, &pa, xa, &pb, xb, n);
    for (int i = 0; i < WORDS; i++)
        if (bufC[i] != bufAsm[i])
            return 1;
    return 0;
}

int main(void) {
    const unsigned widthA = (unsigned)asset_plane_a.widthTiles * TILE_SIZE;
    const unsigned widthB = (unsigned)asset_plane_b.widthTiles * TILE_SIZE;
    const unsigned heightA = (unsigned)asset_plane_a.heightTiles * TILE_SIZE;
    const unsigned heightB = (unsigned)asset_plane_b.heightTiles * TILE_SIZE;
    uint32_t cases = 0, failures = 0;

    /* Ligne 120 de A : sol, beaucoup de lignes de tile mixtes */
    for (unsigned xa = 0; xa < widthA; xa++)
        for (unsigned xb = 0; xb < 32; xb++)
            for (unsigned n = 2; n <= 40; n += 2) {
                failures += (uint32_t)oneCase(120, 120, xa, xb, n);
                cases++;
            }

    for (int k = 0; k < 100000; k++) {
        const int yA = (int)modulo(nextRandom(), heightA);
        const int yB = (int)modulo(nextRandom(), heightB);
        const unsigned xa = modulo(nextRandom(), widthA);
        const unsigned xb = modulo(nextRandom(), widthB);
        const unsigned n = 2 + 2 * modulo(nextRandom(), LINE_SPAN / 2);
        failures += (uint32_t)oneCase(yA, yB, xa, xb, n);
        cases++;
    }

    printNumber("test_merge : cas verifies ", cases);
    printNumber("test_merge : cas faux ", failures);
    return failures != 0;
}
