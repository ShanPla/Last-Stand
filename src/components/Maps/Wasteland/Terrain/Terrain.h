#pragma once

#include "components/Maps/Layout/WastelandLayout/WastelandLayout.h"
#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"

// The open ground of the desert: broad kinds of land that melt into each other, and all
// the small marks that show the player is moving now that no street grid does.
namespace ls::maps::wasteland {

// Blown sand across a road. Grows from 0 to 255, in long streaks along the wind.
int driftNoise(const Desert& desert, int x, int y);

void paintTerrain(Paint& paint, const Desert& desert);

}  // namespace ls::maps::wasteland
