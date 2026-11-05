#pragma once

#include "components/Maps/Maps.h"

// A ruined city: a block grid of broken roads, with a lot of rubble, brick and camps
// in each block. Every choice comes from a hash of the tile and the run's seed.
namespace ls::maps::city {

void paintGroundRows(uint64_t seed, int chunkX, int chunkY, Color* pixels, int rowFrom, int rowTo);
int paintDetails(uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights, CrushedCars crushed, CoverList* covers);

}  // namespace ls::maps::city
