#include "components/Maps/Layout/CityLayout/CityLayout.h"

#include "data/Balance.h"

#include <algorithm>

namespace ls::maps::city {

namespace {

// A lot with no road on one side still keeps this far from that chunk edge. Its walls,
// fences and rubble stand a little past the lot, and the next chunk never paints them, so
// without this room they would be cut flat along the seam. Tall things rise upward, so the
// top edge needs the most.
constexpr int kLotEdgeTop = 24;
constexpr int kLotEdge = 20;

constexpr int kMissingRoadPercent = 12;

}  // namespace

bool verticalRoadExists(uint64_t seed, int i, int j) {
    TileRandom random(seed, i, j, kSaltVertical);
    return !random.chance(kMissingRoadPercent);
}

bool horizontalRoadExists(uint64_t seed, int i, int j) {
    TileRandom random(seed, i, j, kSaltHorizontal);
    return !random.chance(kMissingRoadPercent);
}

Roads roadsAround(uint64_t seed, int cx, int cy) {
    Roads r{};
    r.left = verticalRoadExists(seed, cx, cy);
    r.right = verticalRoadExists(seed, cx + 1, cy);
    r.top = horizontalRoadExists(seed, cx, cy);
    r.bottom = horizontalRoadExists(seed, cx, cy + 1);
    const auto meet = [&](int i, int j) {
        return verticalRoadExists(seed, i, j) || verticalRoadExists(seed, i, j - 1) || horizontalRoadExists(seed, i, j) ||
               horizontalRoadExists(seed, i - 1, j);
    };
    r.corner[0] = meet(cx, cy);
    r.corner[1] = meet(cx + 1, cy);
    r.corner[2] = meet(cx, cy + 1);
    r.corner[3] = meet(cx + 1, cy + 1);
    return r;
}

LotArea lotAreaOf(const Roads& roads, int cx, int cy) {
    const int left = cx * kChunkWidth;
    const int top = cy * kChunkHeight;
    return {left + (roads.left ? kRoadHalfX + kWalkX : kLotEdge), top + (roads.top ? kRoadHalfY + kWalkY : kLotEdgeTop),
            left + kChunkWidth - (roads.right ? kRoadHalfX + kWalkX : kLotEdge), top + kChunkHeight - (roads.bottom ? kRoadHalfY + kWalkY : kLotEdge)};
}

int lotRoll(TileRandom& lotStream) { return lotStream.range(0, 99); }

LotKind lotKindFromRoll(int roll) {
    if (roll < 34) return LotKind::BrickRuin;
    if (roll < 54) return LotKind::PlasterRuin;
    if (roll < 66) return LotKind::Foundation;
    if (roll < 78) return LotKind::Camp;
    if (roll < 90) return LotKind::ParkingLot;
    return LotKind::Park;
}

CarPlan verticalRoadCar(uint64_t seed, int i, int j) {
    CarPlan car;
    if (!verticalRoadExists(seed, i, j)) return car;
    TileRandom random(seed, i, j, kSaltVerticalCar);
    if (!random.chance(balance::kCityVerticalCarPercent)) return car;
    const int y0 = j * kChunkHeight + kRoadHalfY;
    const int y1 = (j + 1) * kChunkHeight - kRoadHalfY;
    const int lane = random.chance(50) ? -kRoadHalfX / 2 : kRoadHalfX / 2;
    car.x = i * kChunkWidth + lane + random.range(-4, 4);
    car.y = random.range(y0 + 52, y1 - 6);
    car.looks = random;
    car.exists = clearOfStart(makeObstacle(CarSprite::CityEnd, car.x, car.y));
    return car;
}

CarPlan horizontalRoadCar(uint64_t seed, int i, int j) {
    CarPlan car;
    if (!horizontalRoadExists(seed, i, j)) return car;
    TileRandom random(seed, i, j, kSaltHorizontalCar);
    if (!random.chance(balance::kCityHorizontalCarPercent)) return car;
    const int x0 = i * kChunkWidth + kRoadHalfX;
    const int x1 = (i + 1) * kChunkWidth - kRoadHalfX;
    const int lane = random.chance(50) ? -kRoadHalfY / 2 + 5 : kRoadHalfY / 2 + 5;
    car.x = random.range(x0 + 40, x1 - 40);
    car.y = j * kChunkHeight + lane;
    car.looks = random;
    car.exists = clearOfStart(makeObstacle(CarSprite::CitySide, car.x, car.y));
    return car;
}

int parkingLotCars(uint64_t seed, int cx, int cy, const LotArea& lot, CarPlan* cars) {
    TileRandom random(seed, cx, cy, kSaltLotCars);
    const int rowY = lot.y0 + (lot.y1 - lot.y0) / 2;
    const int count = random.range(1, kMaxLotCars);
    const int spacing = (lot.x1 - lot.x0 - 80) / std::max(1, count);
    for (int i = 0; i < count; ++i) {
        CarPlan& car = cars[i];
        car.x = lot.x0 + 40 + i * spacing + random.range(-6, 6);
        car.y = i % 2 == 0 ? rowY - 8 : rowY + 26;
        car.looks = TileRandom(seed, cx, cy, kSaltLotCars + 1u + static_cast<uint32_t>(i));
        car.exists = clearOfStart(makeObstacle(CarSprite::CitySide, car.x, car.y));
    }
    return count;
}

void collectObstacles(uint64_t seed, int tileX, int tileY, ObstacleList& list) {
    // A car sits inside its block, so the blocks and roads around this tile cover everything.
    // That is a generous look, and the list only keeps the cars that reach the tile.
    const auto addRoadCar = [&](const CarPlan& car, CarSprite sprite) {
        if (car.exists) list.add(makeObstacle(sprite, car.x, car.y));
    };
    for (int j = tileY - 1; j <= tileY + 1; ++j) {
        for (int i = tileX - 1; i <= tileX + 2; ++i) addRoadCar(verticalRoadCar(seed, i, j), CarSprite::CityEnd);
    }
    for (int j = tileY - 1; j <= tileY + 2; ++j) {
        for (int i = tileX - 1; i <= tileX + 1; ++i) addRoadCar(horizontalRoadCar(seed, i, j), CarSprite::CitySide);
    }
    for (int cy = tileY - 1; cy <= tileY + 1; ++cy) {
        for (int cx = tileX - 1; cx <= tileX + 1; ++cx) {
            TileRandom stream(seed, cx, cy, kSaltLot);
            if (lotKindFromRoll(lotRoll(stream)) != LotKind::ParkingLot) continue;
            CarPlan cars[kMaxLotCars];
            const int count = parkingLotCars(seed, cx, cy, lotAreaOf(roadsAround(seed, cx, cy), cx, cy), cars);
            for (int n = 0; n < count; ++n) addRoadCar(cars[n], CarSprite::CitySide);
        }
    }
}

}  // namespace ls::maps::city
