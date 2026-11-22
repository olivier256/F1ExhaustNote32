#include "hud.h"

#include "fixed.h"
#include "shared.h"
#include "vdp.h"

/* Police : 3 x 5 pixels par chiffre, une rangée de 3 bits par octet
   (pixel de gauche dans le bit 2). Rangées et masques en tables : pas de
   décalage variable (sur SH2, il appellerait libgcc). */
static const uint8_t font3x5[10][5] = {
    {7, 5, 5, 5, 7}, /* 0 */
    {2, 6, 2, 2, 7}, /* 1 */
    {7, 1, 7, 4, 7}, /* 2 */
    {7, 1, 7, 1, 7}, /* 3 */
    {5, 5, 7, 1, 1}, /* 4 */
    {7, 4, 7, 1, 7}, /* 5 */
    {7, 4, 7, 5, 7}, /* 6 */
    {7, 1, 2, 2, 2}, /* 7 */
    {7, 5, 7, 5, 7}, /* 8 */
    {7, 5, 7, 1, 7}, /* 9 */
};
static const uint8_t columnMask[3] = {4, 2, 1};

/* Pour les tests : code froid */
COLD_CODE uint8_t hud_digit_pixel(unsigned digit, int x, int y) {
    /* Chaque pixel de la police fait 2 x 2 ; marge de 1 à gauche, 3 en
       haut : le chiffre occupe x de 1 à 6, y de 3 à 12 */
    const int gx = (x - 1) >> 1, gy = (y - 3) >> 1;
    if (x < 1 || y < 3 || gx >= 3 || gy >= 5)
        return HUD_COLOR_BACK;
    return (font3x5[digit][gy] & columnMask[gx]) ? HUD_COLOR_DIGIT
                                                 : HUD_COLOR_BACK;
}

/* Les chiffres de v, du plus fort au plus faible, par soustractions (pas
   de division sur SH2) */
static void digitsOf(uint32_t v, uint8_t digits[HUD_DIGITS]) {
    static const uint32_t powers[HUD_DIGITS] = {10000, 1000, 100, 10, 1};
    if (v > 99999)
        v = 99999;
    for (int i = 0; i < HUD_DIGITS; i++) {
        uint8_t d = 0;
        while (v >= powers[i]) {
            v -= powers[i];
            d++;
        }
        digits[i] = d;
    }
}

/* Une entrée de la table des sprites */
static void writeSprite(uint32_t offset, int x, int y, int cellsW, int cellsH,
                        unsigned link, unsigned tile) {
    vdp_dram_write16(offset, (uint16_t)(y + 128));
    vdp_dram_write16(offset + 2,
                     (uint16_t)((cellsW - 1) << 10 | (cellsH - 1) << 8 | link));
    vdp_dram_write16(offset + 4, (uint16_t)(0x8000 | tile)); /* priorité,
                                                                palette 0 */
    vdp_dram_write16(offset + 6, (uint16_t)(x + 128));
}

/* Une rangée de 8 pixels d'un chiffre (4 bits par pixel, pixel de gauche
   dans les bits de poids fort), pour chaque rangée de 3 bits de la
   police : marge, deux pixels par bit, marge. Calculée une fois. */
static uint32_t rowPattern[8];
static int rowPatternReady = 0;

static void initRowPatterns(void) {
    for (unsigned bits = 0; bits < 8; bits++) {
        uint32_t row = 0;
        for (int x = 0; x < 8; x++) {
            const int gx = (x - 1) >> 1;
            const int on = x >= 1 && gx < 3 && (bits & columnMask[gx]);
            row = row << 4 | (on ? HUD_COLOR_DIGIT : HUD_COLOR_BACK);
        }
        rowPattern[bits] = row;
    }
    rowPatternReady = 1;
}

uint32_t hud_build(uint32_t position, unsigned stageTiles,
                   unsigned stageEntries) {
    if (!rowPatternReady)
        initRowPatterns();
    uint8_t digits[HUD_DIGITS];
    digitsOf(hud_value(position), digits);

    /* Tiles : chiffre k = tiles 2k (haut) et 2k + 1 (bas), soit 16
       rangées de 4 octets qui se suivent ; une rangée = deux mots. Rangées
       0 à 2 et 13 à 15 : la marge ; 3 à 12 : la police, chaque rangée
       doublée. */
    uint32_t o = MD_STAGE_OFFSET + MD_STAGE_HEADER;
    const uint32_t blank = rowPattern[0];
    for (int k = 0; k < HUD_DIGITS; k++) {
        const uint8_t *glyph = font3x5[digits[k]];
        for (int y = 0; y < 16; y++, o += 4) {
            const int gy = (y - 3) >> 1;
            const uint32_t row =
                (y < 3 || gy >= 5) ? blank : rowPattern[glyph[gy]];
            vdp_dram_write16(o, (uint16_t)(row >> 16));
            vdp_dram_write16(o + 2, (uint16_t)row);
        }
    }
    const uint32_t tileBytes = (uint32_t)HUD_DIGITS * 2 * 32;

    /* Deux sprites : chiffres 1 à 4, puis le cinquième, chaîné à la suite
       s'il y en a une */
    const unsigned first = MD_RESERVED_SPRITES;
    const uint32_t entries =
        MD_STAGE_OFFSET + MD_STAGE_HEADER + stageTiles * 32u;
    writeSprite(entries, HUD_X, HUD_Y, 4, 2, first + 1, MD_SH2_FIRST_TILE);
    writeSprite(entries + 8, HUD_X + 32, HUD_Y, 1, 2,
                stageEntries > HUD_ENTRIES ? first + HUD_ENTRIES : 0,
                MD_SH2_FIRST_TILE + 8);
    return tileBytes + HUD_ENTRIES * 8;
}
