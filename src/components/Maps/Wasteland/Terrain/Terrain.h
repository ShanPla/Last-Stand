#pragma once

#include "components/Maps/Layout/WastelandLayout/WastelandLayout.h"
#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"

// The open ground of the desert: broad kinds of land that melt into each other, and all
// the small marks that show the player is moving now that no street grid does.
namespace ls::maps::wasteland {

// Blown sand across a road. Grows from 0 to 255, in long streaks along the wind.
int driftNoise(const Desert& desert, int x, int y);

// The kind of land under some rows of the chunk. Every row must be done before the marks.
void paintTerrainRows(Paint& paint, const Desert& desert, int rowFrom, int rowTo);
// Cracks, tracks, scattered small things and fences, over the finished land.
void paintTerrainMarks(Paint& paint, const Desert& desert);

}  // namespace ls::maps::wasteland
