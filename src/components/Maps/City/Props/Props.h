#pragma once

#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"
#include "components/Maps/SharedProps/SharedProps.h"

// Things standing or lying around the city, seen from the same 3/4 angle as the
// characters. x and y are where the object meets the ground, usually its middle.
namespace ls::maps::city {

void streetLamp(Paint& p, int x, int y, bool armToRight, bool broken, LightList& lights);
void stopSign(Paint& p, int x, int y, int lean);
void brokenSign(Paint& p, int x, int y, TileRandom& random);
void carSide(Paint& p, int x, int y, TileRandom& random);
void carEnd(Paint& p, int x, int y, TileRandom& random);
void tires(Paint& p, int x, int y);
void barrier(Paint& p, int x, int y, TileRandom& random);
void sofa(Paint& p, int x, int y, TileRandom& random);
void table(Paint& p, int x, int y, TileRandom& random);
void bedFrame(Paint& p, int x, int y, TileRandom& random);
void bench(Paint& p, int x, int y);
void deadTree(Paint& p, int x, int y, TileRandom& random);
void fenceAcross(Paint& p, int x0, int x1, int y, TileRandom& random);
void fenceAlong(Paint& p, int x, int y0, int y1, TileRandom& random);
void looseBricks(Paint& p, int x, int y, int radius, int count, TileRandom& random);
void beam(Paint& p, int x0, int y0, int x1, int y1);
void weedTuft(Paint& p, int x, int y, TileRandom& random);
void puddle(Paint& p, int x, int y, int rx, int ry);

}  // namespace ls::maps::city
