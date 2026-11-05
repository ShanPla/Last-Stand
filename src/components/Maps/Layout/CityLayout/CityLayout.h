#pragma once

#include "components/Maps/Layout/Layout.h"

// How the city's blocks and its cars are laid out. It holds no pixels: the painters in
// Maps/City and the simulation both read it, so they always agree about where a car is.
namespace ls::maps::city {

// Roads run along chunk edges, so a road's center line sits exactly on a seam, and
// each chunk holds a whole block. All sizes are in chunk texels.
inline constexpr int kRoadHalfX = 48;  // A road running up the screen: half its width.
inline constexpr int kRoadHalfY = 28;  // A road running across the screen: half its height.
inline constexpr int kWalkX = 20;      // Sidewalks beside those roads.
inline constexpr int kWalkY = 13;

// Salts that keep each kind of random choice independent of the others. Cars have streams of
// their own, because the simulation needs them without working through a whole painter.
enum Salt : uint32_t {
    kSaltVertical = 11,
    kSaltHorizontal = 23,
    kSaltLot = 37,
    kSaltVerticalProps = 41,
    kSaltHorizontalProps = 53,
    kSaltCorner = 67,
    kSaltVerticalCar = 71,
    kSaltHorizontalCar = 79,
    kSaltLotCars = 83,
};

bool verticalRoadExists(uint64_t seed, int i, int j);
bool horizontalRoadExists(uint64_t seed, int i, int j);

// Which roads touch this chunk.
struct Roads {
    bool left, right, top, bottom;
    bool corner[4];  // Top left, top right, bottom left, bottom right.
};

Roads roadsAround(uint64_t seed, int cx, int cy);

struct LotArea {
    int x0, y0, x1, y1;  // From (x0, y0) up to but not including (x1, y1).
};

// The inside of a block, in whole-map texels.
LotArea lotAreaOf(const Roads& roads, int cx, int cy);

// A lot's kind is the first roll of its stream, so it can be asked for without painting.
enum class LotKind { BrickRuin, PlasterRuin, Foundation, Camp, ParkingLot, Park };
LotKind lotKindFromRoll(int roll);
int lotRoll(TileRandom& lotStream);

// The car standing on a road, if any.
CarPlan verticalRoadCar(uint64_t seed, int i, int j);
CarPlan horizontalRoadCar(uint64_t seed, int i, int j);

// The cars parked in a parking lot. Fills cars and returns how many were planned.
inline constexpr int kMaxLotCars = 3;
int parkingLotCars(uint64_t seed, int cx, int cy, const LotArea& lot, CarPlan* cars);

void collectObstacles(uint64_t seed, int tileX, int tileY, ObstacleList& list);

}  // namespace ls::maps::city
