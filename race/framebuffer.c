#include "framebuffer.h"

#include <stdint.h>

/* Des mots dans l'ordre du SH2, que le VDP (et le simulateur) relit tels
   quels */
void fb_init_line_table(int screenHeight, uint32_t offset) {
    for (int y = 0; y < screenHeight; y++)
        vdp_dram_write16((uint32_t)y * 2, (uint16_t)(offset >> 1));
}
