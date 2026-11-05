#pragma once

#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"
#include "components/Maps/SharedProps/SharedProps.h"
#include "components/Maps/Wasteland/Terrain/Terrain.h"

// The broken highways. Where they run is the layout's business, and this paints them.
namespace ls::maps::wasteland {

void paintRoads(Paint& paint, const Desert& desert, LightList& lights);

}  // namespace ls::maps::wasteland
