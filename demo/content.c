/* =========================================================================
   Démo : le circuit de demo/track.txt.
   ========================================================================= */

#include "race.h"

#include "demo_track.h" /* généré par track2c depuis demo/track.txt */

const RaceContent race_content = {
    .track = &demo_track,
};
