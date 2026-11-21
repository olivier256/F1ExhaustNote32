/* =========================================================================
   Démo : décor de Chun-Li (Street Fighter Alpha), deux plans.
   - Plan A (devant) : la Muraille et le sol, 768 x 224, ciel transparent.
   - Plan B (derrière) : les montagnes, 672 x 224.
   ========================================================================= */

#include "supervdp.h"

#include "demo_assets.h" /* généré par bmp2c depuis demo/assets/assets.txt */

const SuperVdpContent supervdp_content = {
    .tiles = &asset_tiles,
    .planeA = &asset_plane_a,
    .planeB = &asset_plane_b,
    .palette = asset_palette,
};
