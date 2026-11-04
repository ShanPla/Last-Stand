#pragma once

#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"
#include "components/Maps/SharedProps/SharedProps.h"

// Things lying around the desert, seen from the same 3/4 angle as the characters. x and
// y are where the object meets the ground. Nothing here is taller than a zombie's chest,
// apart from thin poles and signs, because the whole map draws under the characters.
namespace ls::maps::wasteland {

// Small ground texture.
void stone(Paint& p, int x, int y, int size, TileRandom& random);
void pebbles(Paint& p, int x, int y, int spread, int count, TileRandom& random);
void tuft(Paint& p, int x, int y, TileRandom& random);
void bush(Paint& p, int x, int y, TileRandom& random);
void tumbleweed(Paint& p, int x, int y, TileRandom& random);
void sandMound(Paint& p, int x, int y, int rx, int ry);
void tirePile(Paint& p, int x, int y, TileRandom& random);
void litter(Paint& p, int x, int y, TileRandom& random);
// A soft dark patch on the ground, with no hard edge.
void groundStain(Paint& p, int x, int y, int rx, int ry, Color color);

// Rocks.
void boulder(Paint& p, int x, int y, int halfWidth, TileRandom& random);
void rockShelf(Paint& p, int x, int y, int halfWidth, int height, bool red, TileRandom& random);

// Wrecks. "Side" ones sit on a road running across the screen, and "end" ones on a road
// running up it, seen from behind and above.
void carSide(Paint& p, int x, int y, TileRandom& random);
void carOnRoof(Paint& p, int x, int y, TileRandom& random);
void carEnd(Paint& p, int x, int y, TileRandom& random);
void busSide(Paint& p, int x, int y, TileRandom& random);
void busEnd(Paint& p, int x, int y, TileRandom& random);
void tankerSide(Paint& p, int x, int y, TileRandom& random);
// A wreck on fire, on top of whichever car the caller drew. Its light comes on at dusk.
void wreckFire(Paint& p, int x, int y, LightList& lights);

// By the road.
void telephonePole(Paint& p, int x, int y, int lean, bool snapped);
void powerWire(Paint& p, int x0, int y0, int x1, int y1);
void roadPost(Paint& p, int x, int y);
void guardRailAcross(Paint& p, int x0, int x1, int y, TileRandom& random);
void guardRailAlong(Paint& p, int x, int y0, int y1, TileRandom& random);
void guideSign(Paint& p, int x, int y, const char* words, TileRandom& random);
void warningSign(Paint& p, int x, int y, TileRandom& random);
void plateSign(Paint& p, int x, int y, TileRandom& random);
void billboard(Paint& p, int x, int y, const char* words, TileRandom& random);
void pumpIsland(Paint& p, int x, int y, TileRandom& random);
void lampPost(Paint& p, int x, int y, LightList& lights);
void poleSign(Paint& p, int x, int y, const char* words, TileRandom& random);
// Small blocky letters, with some rubbed away. Only the letters the signs use exist.
void signText(Paint& p, int x, int y, const char* words, Color color, int wearOutOf16, int size = 1);

// Fences. The wire strands sag between the posts, and some of them have fallen.
void wireFenceAcross(Paint& p, int x0, int x1, int y);
void wireFenceAlong(Paint& p, int x, int y0, int y1);

// Trees and wood.
void deadTree(Paint& p, int x, int y, TileRandom& random);
void fallenLog(Paint& p, int x, int y, TileRandom& random);
void stump(Paint& p, int x, int y);

// Bones.
void cowSkull(Paint& p, int x, int y);
void ribCage(Paint& p, int x, int y, int ribs, int size);
void bigSkeleton(Paint& p, int x, int y, TileRandom& random);
void humanSkeleton(Paint& p, int x, int y, TileRandom& random);
void looseBones(Paint& p, int x, int y, int spread, int count, TileRandom& random);

// Low ruined walls. A face wall runs across the screen and shows its front, and a side
// wall runs up the screen and mostly shows its top. tones must hold three colors.
struct WallLook {
    const Color* tones;
    Color top;
    int ribEvery;    // Vertical ribs for sheet metal, or 0 for block courses.
    int courseRows;  // Rows of block, or 0 for none.
};
void faceWall(Paint& p, TileRandom& random, const WallLook& look, int x0, int x1, int footY, int maxHeight);
void sideWall(Paint& p, TileRandom& random, const WallLook& look, int x, int y0, int y1, int maxHeight);
void roofSheet(Paint& p, TileRandom& random, int x, int y, int w, int h);

}  // namespace ls::maps::wasteland
