/* =========================================================================
   Démo : le circuit de demo/track.txt et ses objets (demo/assets/props,
   produits par tools/props.py).
   ========================================================================= */

#include "race.h"

#include "demo_assets.h" /* généré par bmp2c depuis demo/assets/assets.txt */
#include "demo_track.h"  /* généré par track2c depuis demo/track.txt */

/* Dans l'ordre des types de track2c : tires, billboard, palm. Tailles
   dans le monde (la route a une demi-largeur de 768). */
static const PropKind props[] = {
    {{&asset_tires_0, &asset_tires_1, &asset_tires_2}, 256, 256},
    {{&asset_billboard_0, &asset_billboard_1, &asset_billboard_2}, 1536, 768},
    {{&asset_palm_0, &asset_palm_1, &asset_palm_2}, 640, 1280},
};

const RaceContent race_content = {
    .track = &demo_track,
    .props = props,
    .propCount = sizeof props / sizeof props[0],
};
