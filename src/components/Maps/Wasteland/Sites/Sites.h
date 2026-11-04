#pragma once

#include "components/Maps/Paint/Paint.h"
#include "components/Maps/SharedProps/SharedProps.h"
#include "components/Maps/Wasteland/Terrain/Terrain.h"

// The places worth finding on a long walk: gas stops by the highway, shack clusters, rock
// outcrops, dead groves, bone fields, wreck yards, old foundations and abandoned camps.
// Each is placed by a hash of a coarse grid cell, so it never needs storing.
namespace ls::maps::wasteland {

void paintSites(Paint& paint, const Desert& desert, LightList& lights);

}  // namespace ls::maps::wasteland
