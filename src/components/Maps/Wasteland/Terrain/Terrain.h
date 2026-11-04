#pragma once

#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"

// The open ground of the desert: broad kinds of land that melt into each other, and all
// the small marks that show the player is moving now that no street grid does.
namespace ls::maps::wasteland {

// Division that rounds down, so that negative map coordinates work like positive ones.
inline int floorDiv(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}
inline int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }

enum class Zone : uint8_t { Dunes, Sand, Pan, Reg, Scrub, RedEarth };

// The seed slides the large noise fields, so that each run gets its own geography.
struct Desert {
    uint64_t seed;
    int ax, ay, bx, by, windX, windY;
};

Desert desertFor(uint64_t seed);

// What kind of land is at a texel. Depends only on the seed and the place.
Zone zoneAt(const Desert& desert, int x, int y);

// Blown sand across a road. Grows from 0 to 255, in long streaks along the wind.
int driftNoise(const Desert& desert, int x, int y);

void paintTerrain(Paint& paint, const Desert& desert);

}  // namespace ls::maps::wasteland
