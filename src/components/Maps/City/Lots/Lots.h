#pragma once

#include "components/Maps/City/Props/Props.h"
#include "components/Maps/Layout/CityLayout/CityLayout.h"
#include "components/Maps/Paint/Paint.h"

// What fills the inside of a city block: brick and plaster ruins seen from above,
// parking lots, camps, dead parks and bare foundations.
namespace ls::maps::city {

// cars are what the layout parks here, if this lot turns out to be a parking lot.
void paintLot(Paint& paint, TileRandom& random, const LotArea& lot, CarPlan* cars, int carCount, LightList& lights);

}  // namespace ls::maps::city
