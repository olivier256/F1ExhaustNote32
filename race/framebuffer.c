#include "framebuffer.h"

#include <stdint.h>

/* La table de lignes est écrite comme le reste du frame buffer, par
   vdp_dram_write16 : des mots dans l'ordre du SH2, que le VDP (et le
   simulateur) relit tels quels. Elle n'est écrite qu'une fois pour
   l'instant ; elle pourra l'être à chaque image (une ligne d'écran
   montrera alors n'importe quelle ligne préparée). */
void fb_init_line_table(int screenHeight) {
    for (int y = 0; y < screenHeight; y++)
        vdp_dram_write16((uint32_t)y * 2,
                         (uint16_t)((FB_LINE_TABLE_BYTES + y * FB_PITCH) / 2));
}
