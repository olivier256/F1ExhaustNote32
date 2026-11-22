#include "mdvdp.h"
#include "interface.h"
#include "platform.h"
#include "vdp.h"

#include <stdio.h>
#include <stdlib.h>

/* =========================================================================
   VDP Mega Drive (émulé) : voir mdvdp.h.
   ========================================================================= */

static uint8_t _vram[MD_VRAM_SIZE];
static uint16_t _cram[MD_CRAM_COLORS];
static uint16_t _spriteTable = 0xD800;

/* Synchro vidéo : ligne en cours, VBlank */
static volatile int _line = 0;
static volatile int _inVBlank = 0;
static unsigned long _vblanks = 0;

/* DMA : octets restant à transférer (le DMA avance ligne par ligne) */
static volatile uint32_t _dmaRemaining = 0;
static unsigned long _dmaVBlank = (unsigned long)-1; /* VBlank du dernier DMA */

/* Diagnostics */
static md_vdp_stats _stats;
static uint32_t _vblankBytes = 0, _vblankLines = 0;

static void fail(const char *what) {
    fprintf(stderr, "VDP Mega Drive : %s\n", what);
    abort();
}

/* ---- Vu du 68000 -----------------------------------------------------------
 */

void md_vdp_set_sprite_table(uint16_t vramAddress) {
    _spriteTable = vramAddress & 0xFC00;
}

void md_vdp_write_vram(uint16_t vramAddress, uint16_t word) {
    if (vramAddress & 1)
        fail("ecriture VRAM a une adresse impaire");
    _vram[vramAddress] = (uint8_t)(word >> 8);
    _vram[vramAddress + 1] = (uint8_t)word;
}

void md_vdp_write_cram(int index, uint16_t color) {
    if (index < 0 || index >= MD_CRAM_COLORS)
        fail("index CRAM hors limites");
    _cram[index] = color & 0x0EEE;
}

/* Le 68000 voit le frame buffer 32X qui n'est pas affiché (vdp_dram), à
   condition d'avoir le VDP 32X */
static const uint8_t *frameBuffer32X(uint32_t offset, uint32_t bytes,
                                     const char *what) {
    interface_check_vdp_access(what);
    if (offset + bytes > VDP_DRAM_SIZE)
        fail("lecture hors du frame buffer 32X");
    return (const uint8_t *)vdp_dram + offset;
}

uint16_t md_read_32x_fb16(uint32_t fbOffset) {
    if (fbOffset & 1)
        fail("lecture 16 bits a une adresse impaire du frame buffer 32X");
    const uint8_t *p = frameBuffer32X(fbOffset, 2, "lecture du frame buffer");
    return (uint16_t)(p[0] << 8 | p[1]);
}

void md_vdp_dma_from_32x(uint32_t fbOffset, uint16_t vramAddress,
                         uint32_t bytes) {
    if ((fbOffset | vramAddress | bytes) & 1)
        fail("DMA : adresses et longueur paires");
    if ((uint32_t)vramAddress + bytes > MD_VRAM_SIZE)
        fail("DMA : au-delà de la VRAM");
    const uint8_t *src = frameBuffer32X(fbOffset, bytes, "DMA");

    /* Les données arrivent tout de suite en VRAM (le simulateur n'affiche
       rien pendant le VBlank) ; le temps du transfert est compté à part,
       ligne par ligne, et le 68000 attend. */
    for (uint32_t i = 0; i < bytes; i++)
        _vram[vramAddress + i] = src[i];
    _dmaRemaining += bytes;

    _stats.dmas++;
    if (_inVBlank) {
        _dmaVBlank = _vblanks;
        _vblankBytes += bytes;
    }
}

int md_vdp_dma_busy(void) { return _dmaRemaining > 0; }

/* ---- Synchro vidéo
 * ------------------------------------------------------------ */

void md_vdp_begin_line(int line, int inVBlank) {
    if (inVBlank && !_inVBlank) { /* début du VBlank */
        _vblanks++;
        _vblankBytes = 0;
        _vblankLines = 0;
    }
    if (!inVBlank && _inVBlank && _vblankBytes > 0) { /* fin du VBlank */
        _stats.lastVBlankBytes = _vblankBytes;
        _stats.lastVBlankLines = _vblankLines;
        if (_vblankBytes > _stats.maxVBlankBytes)
            _stats.maxVBlankBytes = _vblankBytes;
    }
    _line = line;
    _inVBlank = inVBlank;

    /* Le DMA a eu toute la ligne précédente */
    if (_dmaRemaining > 0) {
        const uint32_t rate = inVBlank ? MD_DMA_BYTES_PER_VBLANK_LINE
                                       : MD_DMA_BYTES_PER_DISPLAY_LINE;
        if (!inVBlank && line == 0)
            _stats.dmaInDisplay++; /* le VBlank n'a pas suffi */
        _dmaRemaining = _dmaRemaining > rate ? _dmaRemaining - rate : 0;
        if (inVBlank)
            _vblankLines++;
    }
}

int md_vdp_dma_in_this_vblank(void) {
    return _inVBlank && _dmaVBlank == _vblanks;
}

/* ---- Affichage des sprites
 * ------------------------------------------------------ */

static uint16_t vramWord(uint32_t address) {
    address &= MD_VRAM_SIZE - 2;
    return (uint16_t)(_vram[address] << 8 | _vram[address + 1]);
}

/* Couleur Mega Drive (3 bits par composante) -> 0x00RRGGBB */
static uint32_t rgbOf(uint16_t color) {
    static const uint8_t level[8] = {0, 36, 73, 109, 146, 182, 219, 255};
    return (uint32_t)level[(color >> 1) & 7] << 16 |
           (uint32_t)level[(color >> 5) & 7] << 8 | level[(color >> 9) & 7];
}

void md_vdp_compose_line(int line, uint32_t *rgb, const uint8_t *through,
                         int megaDriveInFront) {
    /* 1. Les sprites de la ligne, dans l'ordre de la liste (le premier
          devant), 20 au plus */
    int found[MD_SPRITES_PER_LINE];
    int count = 0, overflow = 0;
    unsigned entry = 0;
    for (int examined = 0; examined < MD_SPRITES; examined++) {
        const uint32_t base = _spriteTable + entry * 8u;
        const int y = (vramWord(base) & 0x3FF) - MD_SPRITE_OFFSET;
        const uint16_t sizeLink = vramWord(base + 2);
        const int height = (((sizeLink >> 8) & 3) + 1) * 8;
        if (line >= y && line < y + height) {
            if (count == MD_SPRITES_PER_LINE) {
                overflow = 1;
                break;
            }
            found[count++] = (int)entry;
        }
        entry = sizeLink & 0x7F;
        if (entry == 0 || entry >= MD_SPRITES)
            break;
    }

    /* 2. Les pixels, au plus 320 de sprites : du dernier au premier, pour
          que le premier soit devant */
    uint8_t colorIndex[VDP_SCREEN_WIDTH] = {0};
    int pixels = 0, drawn = count;
    for (int i = 0; i < count; i++) {
        const uint16_t sizeLink = vramWord(_spriteTable + found[i] * 8u + 2);
        pixels += (((sizeLink >> 10) & 3) + 1) * 8;
        if (pixels > MD_SPRITE_PIXELS_PER_LINE) {
            drawn = i; /* ce sprite et les suivants ne sont pas dessinés */
            overflow = 1;
            break;
        }
    }
    for (int i = drawn - 1; i >= 0; i--) {
        const uint32_t base = _spriteTable + found[i] * 8u;
        const int y = (vramWord(base) & 0x3FF) - MD_SPRITE_OFFSET;
        const uint16_t sizeLink = vramWord(base + 2);
        const uint16_t attr = vramWord(base + 4);
        const int x = (vramWord(base + 6) & 0x1FF) - MD_SPRITE_OFFSET;
        const int cellsW = ((sizeLink >> 10) & 3) + 1;
        const int cellsH = ((sizeLink >> 8) & 3) + 1;
        const int palette = (attr >> 13) & 3;
        const int vflip = (attr >> 12) & 1, hflip = (attr >> 11) & 1;
        const unsigned tile = attr & 0x7FF;

        int row = line - y;
        if (vflip)
            row = cellsH * 8 - 1 - row;
        for (int col = 0; col < cellsW * 8; col++) {
            const int px = x + col;
            if (px < 0 || px >= VDP_SCREEN_WIDTH)
                continue;
            const int c = hflip ? cellsW * 8 - 1 - col : col;
            const unsigned t = tile + (unsigned)(c >> 3) * cellsH + (row >> 3);
            const uint8_t b =
                _vram[(t * MD_TILE_BYTES + (row & 7) * 4 + ((c & 7) >> 1)) &
                      (MD_VRAM_SIZE - 1)];
            const int pixel = (c & 1) ? (b & 15) : (b >> 4);
            if (pixel != 0)
                colorIndex[px] = (uint8_t)(palette * 16 + pixel);
        }
    }
    if (overflow)
        _stats.spriteOverflowLines++;

    /* 3. Superposition avec l'image 32X */
    for (int px = 0; px < VDP_SCREEN_WIDTH; px++)
        if (colorIndex[px] != 0 && (megaDriveInFront || through[px]))
            rgb[px] = rgbOf(_cram[colorIndex[px]]);
}

void md_vdp_get_stats(md_vdp_stats *stats, int resetMax) {
    *stats = _stats;
    if (resetMax)
        _stats.maxVBlankBytes = 0;
}
