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
// The map and the flattened cars come from the run, so the picture and the solid cars can't
// disagree. The same seed and the same flattened cars always give the same picture.
void updateGround(const View& view, uint64_t seed, maps::MapId map, maps::CrushedCars crushed);

void drawGround(const View& view, const DayLook& look);

// Puts the pixels of the cars and standing objects on screen into the depth buffer, so they
// hide what stands behind them. A car's pixels all take the depth of its near edge, and each
// pixel of a prop the depth of its own foot. It changes no colors, so everything stays exactly
// as painted, and it needs no memory beyond the chunks: the marks are in their spare alpha.
void drawCoverDepth(const View& view);

// Where the standing objects are in the picture, for the figures that have to show through.
struct CoverOnScreen {
    float left, top, right, bottom;  // Its picture, in art pixels.
    float feetRow;                   // Row of its nearest foot. A figure above this may be behind it.
    float depth;
    bool car;                        // A car is big enough to hide a whole figure.
};

inline constexpr int kMaxCoversOnScreen = 512;
int coversOnScreen(const View& view, CoverOnScreen* covers, int capacity);

// For the tests: objects that did not fit in a list of covers on screen, since the start.
int coversDropped();

// Debug only: a standing object found for a test scene, in map texels.
struct FoundCover {
    bool found = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int footX = 0;
    int nearRow = 0;
    int spotX = 0;  // Where a scene's zombie stands: a wall's middle, or a pole's foot.
};

// Debug only: the first object of a kind at least minWide by minTall texels, in widening rings
// around the start, standing wholly inside one chunk and clear of every car and other standing
// object. It paints the
// chunks it looks at, so it is far too slow for play.
FoundCover findCover(maps::MapId map, uint64_t seed, maps::CoverKind kind, int minWide, int minTall);

// Debug only: remembers the scene's object, so the tests can ask where it is on screen.
void setSceneCover(const FoundCover& cover);
// Its picture in art pixels: left, top, right, bottom, then the zombie's spot at its foot.
// False when there is none.
bool sceneCoverOnScreen(const View& view, float out[6]);

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

// For the browser tests: how many chunks changed to a flat car's picture in the same frame.
int groundLastSwapSize();

}  // namespace ls::worldview
