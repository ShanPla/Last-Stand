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

// A shadow that holds an object to the ground: a soft ring and a darker core under its foot.
void contactShadow(Paint& p, int x, int y, int rx, int ry);
// A round post or pole of tones dark, middle and light, lit on the left. lean moves its top.
void post(Paint& p, int x, int y, int height, int width, int lean, const Color tones[3]);
// A box standing on the ground, its front face on columns x0 to x1 - 1 with its foot at footY.
// Its lit top runs depth rows back up the screen, leaning right, over a shaded right side.
void standingBox(Paint& p, int x0, int x1, int footY, int height, int depth, Color front);
void barrel(Paint& p, int x, int y, TileRandom& random);
void burningBarrel(Paint& p, int x, int y, TileRandom& random, LightList& lights);
void tent(Paint& p, int x, int y, TileRandom& random);
void campfire(Paint& p, int x, int y, LightList& lights);
void crate(Paint& p, int x, int y, TileRandom& random);
void boards(Paint& p, int x, int y, TileRandom& random);
void sandbags(Paint& p, int x, int y, int count, TileRandom& random);
void rubblePile(Paint& p, int x, int y, int radius, TileRandom& random);

}  // namespace ls::maps
