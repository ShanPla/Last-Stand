#pragma once

#include "components/Maps/Maps.h"

// An endless desert: dunes, cracked pans and gravel plains crossed by broken highways,
// with the odd gas stop, shack cluster or field of bones to find along the way. Every
// choice comes from a hash of a place and the run's seed.
namespace ls::maps::wasteland {

int renderChunk(uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights);

}  // namespace ls::maps::wasteland
