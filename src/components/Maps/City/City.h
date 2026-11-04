#pragma once

#include "components/Maps/Maps.h"

// A ruined city: a block grid of broken roads, with a lot of rubble, brick and camps
// in each block. Every choice comes from a hash of the tile and the run's seed.
namespace ls::maps::city {

int renderChunk(uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights);

}  // namespace ls::maps::city
