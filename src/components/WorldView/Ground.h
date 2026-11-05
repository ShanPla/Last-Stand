#pragma once

#include "components/Maps/Maps.h"
#include "components/WorldView/DayCycle.h"
#include "components/WorldView/Projection.h"

#include <cstdint>

// Keeps a small pool of painted chunks as textures, and draws the visible ones as a
// handful of quads. Painting a chunk takes real time, so one is painted at a time, ahead of
// the camera and a piece per frame, and crossing a chunk edge never causes a hitch.
namespace ls::worldview {

void initGround();

// Paints what the next frames will need. Must run outside any drawing pass.
// The map comes from the run, so the picture and the solid cars can't disagree.
void updateGround(const View& view, uint64_t seed, maps::MapId map);

void drawGround(const View& view, const DayLook& look);

// Puts the pixels of the cars on screen into the depth buffer, each at its own depth, so they
// hide what stands behind them. It changes no colors, so the cars stay exactly as painted,
// and it needs no memory beyond the chunks: the marks are in their spare alpha channel.
void drawCarDepth(const View& view);

// Where the cars are in the picture, for the figures that have to show through them.
struct CarOnScreen {
    float left, top, right, bottom;  // The whole picture of the car, in art pixels.
    float feetRow;                   // Row of its near edge. A figure above this is behind it.
    float depth;
    Vec2 boxMin, boxMax;             // Its solid box, in the world.
};

inline constexpr int kMaxCarsOnScreen = 64;
int carsOnScreen(const View& view, CarOnScreen* cars);

// Pools of light from lamps and fires. Goes in the palette shader, with multiplying blending.
void drawLightPools(const View& view, const DayLook& look, uint32_t tick, bool flicker);
// Bulbs and flames, drawn as sprites. Goes in the palette shader's pass.
void drawLightSprites(const View& view, const DayLook& look, uint32_t tick, bool flicker);

// quality 0 paints chunks at half resolution, to save memory on weak devices.
void setGroundHalfResolution(bool half);

// For the browser tests: chunks painted so far. Standing still, this must stop rising.
int groundPaintCount();

}  // namespace ls::worldview
