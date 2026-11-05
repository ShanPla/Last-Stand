#pragma once

#include "components/Maps/Layout/Layout.h"

#include <cstdint>

// A car the boss has walked over, painted as a flat wreck. There is no second set of art: the
// car's own painter runs as usual, but into a small private picture instead of the chunk.
// That picture is then pressed flat and stamped on the ground. So every car, bus and tanker
// keeps its own colors and shape, with no extra painter to write for each one.
//
// The private picture is the same whichever chunk asks for it, and the stamp uses map
// positions only, so a flat car crossing a chunk's edge lines up on both sides.
namespace ls::maps {

class Paint;

// Called by CarMark. Between these two calls, whatever is painted goes to the private picture.
void beginFlatCar(Paint& paint, CarSprite sprite, int x, int y);
void finishFlatCar(Paint& paint, CarSprite sprite, int x, int y);

// How far from a car's picture its flat version and its leftover bits can reach, in chunk
// texels, for a test that nothing else on the map changes.
TexelRect flatCarReach(const Obstacle& obstacle);

// For a browser test: while out is set, each private picture's checksum is written there, to
// show that the two chunks on either side of an edge really do start from the same picture.
void setFlatCarProbe(uint32_t* out);

}  // namespace ls::maps
