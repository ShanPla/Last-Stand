#pragma once

#include "components/Maps/Maps.h"
#include "components/WorldView/DayCycle.h"
#include "components/WorldView/Projection.h"

#include <cstdint>

// Keeps a small pool of painted chunks as textures, and draws the visible ones as a
// handful of quads. Painting a chunk takes real time, so at most one is painted per
// frame, ahead of the camera, and crossing a chunk edge never causes a hitch.
namespace ls::worldview {

void initGround();

// Paints what the next frames will need. Must run outside any drawing pass.
void updateGround(const View& view, uint64_t seed);

void drawGround(const View& view, const DayLook& look);

// Pools of light from lamps and fires. Goes in the palette shader, with multiplying blending.
void drawLightPools(const View& view, const DayLook& look, uint32_t tick, bool flicker);
// Bulbs and flames, drawn as sprites. Goes in the palette shader's pass.
void drawLightSprites(const View& view, const DayLook& look, uint32_t tick, bool flicker);

// quality 0 paints chunks at half resolution, to save memory on weak devices.
void setGroundHalfResolution(bool half);

}  // namespace ls::worldview
