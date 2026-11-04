#pragma once

#include "components/Maps/Paint/Paint.h"
#include "components/Maps/City/Props/Props.h"

// What fills the inside of a city block: brick and plaster ruins seen from above,
// parking lots, camps, dead parks and bare foundations.
namespace ls::maps::city {

struct LotArea {
    int x0, y0, x1, y1;  // From (x0, y0) up to but not including (x1, y1).
};

void paintLot(Paint& paint, TileRandom& random, const LotArea& lot, LightList& lights);

}  // namespace ls::maps::city
