/* =========================================================================
   Démo : le circuit de demo/track.txt, sous le décor de demo/assets/sky.bmp.
   ========================================================================= */

#include "race.h"

#include "demo_assets.h" /* généré par bmp2c depuis demo/assets/assets.txt */
#include "demo_track.h"  /* généré par track2c depuis demo/track.txt */

const RaceContent race_content = {
    .track = &demo_track,
    .sky = &asset_sky,
    .palette = asset_palette,
};
