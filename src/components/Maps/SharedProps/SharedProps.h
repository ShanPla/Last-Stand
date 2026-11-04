#pragma once

#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"

// Props that look at home on any map, and the list that collects a map's live lights.
// x and y are where the object meets the ground, usually its middle.
namespace ls::maps {

// Collects the map's live lights while it paints.
struct LightList {
    Light* lights;
    int count = 0;
    const Paint* area;

    void add(int x, int y, float height, LightKind kind);
};

void contactShadow(Paint& p, int x, int y, int rx, int ry);
void barrel(Paint& p, int x, int y, TileRandom& random);
void burningBarrel(Paint& p, int x, int y, TileRandom& random, LightList& lights);
void tent(Paint& p, int x, int y, TileRandom& random);
void campfire(Paint& p, int x, int y, LightList& lights);
void crate(Paint& p, int x, int y, TileRandom& random);
void boards(Paint& p, int x, int y, TileRandom& random);
void sandbags(Paint& p, int x, int y, int count, TileRandom& random);
void rubblePile(Paint& p, int x, int y, int radius, TileRandom& random);

}  // namespace ls::maps
