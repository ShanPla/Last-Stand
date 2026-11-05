#include "components/Maps/Layout/Layout.h"

#include "components/Maps/Layout/CityLayout/CityLayout.h"
#include "components/Maps/Layout/WastelandLayout/WastelandLayout.h"
#include "data/Balance.h"

#include <algorithm>

namespace ls::maps {

namespace {

// The ground each picture stands on, in chunk texels, measured from the point its painter
// is given (its feet, at the bottom middle). These sizes come from the painted sprites, so
// they live here and not with the balance numbers. Tall parts of a picture rise above their
// footprint and are not solid: that is what lets a zombie walk behind a car.
struct Footprint {
    int halfWide;   // Texels either side of the feet.
    int rowsAbove;  // Texels from the feet up to the far edge of the car on the ground.
    int rowsBelow;  // Texels from the feet down to its near edge.
    int spriteTop;  // How far the picture rises above the feet.
    int shear;      // How far a tilted wreck can slide its columns up or down.
};

Footprint footprintOf(CarSprite sprite) {
    switch (sprite) {
        case CarSprite::CitySide: return {28, 11, 2, 23, 0};
        case CarSprite::CityEnd: return {16, 38, 2, 47, 0};
        case CarSprite::WreckSide: return {27, 11, 2, 23, 5};
        case CarSprite::WreckOnRoof: return {26, 11, 2, 15, 4};
        case CarSprite::WreckEnd: return {16, 38, 2, 47, 0};
        case CarSprite::BusSide: return {48, 14, 2, 28, 3};
        case CarSprite::BusEnd: return {19, 58, 2, 67, 0};
        case CarSprite::TankerSide: return {58, 14, 2, 26, 3};
    }
    return {28, 11, 2, 23, 0};
}

}  // namespace

Obstacle makeObstacle(CarSprite sprite, int texelX, int texelY) {
    const Footprint size = footprintOf(sprite);
    const float top = static_cast<float>(texelY - size.rowsAbove) / kTexelsPerWorldY;
    const float bottom = static_cast<float>(texelY + size.rowsBelow) / kTexelsPerWorldY;
    Obstacle obstacle;
    obstacle.center = {static_cast<float>(texelX) / kTexelsPerWorldX, (top + bottom) * 0.5f};
    obstacle.half = {static_cast<float>(size.halfWide) / kTexelsPerWorldX, (bottom - top) * 0.5f};
    obstacle.texelX = texelX;
    obstacle.texelY = texelY;
    obstacle.sprite = sprite;
    return obstacle;
}

bool clearOfStart(const Obstacle& obstacle) {
    // The player starts at the world's origin. This is the gap from there to the box.
    const float gapX = std::max(std::abs(obstacle.center.x) - obstacle.half.x, 0.0f);
    const float gapY = std::max(std::abs(obstacle.center.y) - obstacle.half.y, 0.0f);
    return gapX * gapX + gapY * gapY >= balance::kCarClearOfStart * balance::kCarClearOfStart;
}

TexelRect spriteRect(const Obstacle& obstacle) {
    const Footprint size = footprintOf(obstacle.sprite);
    constexpr int kWheelRoom = 3;  // Wheels and bumpers stick out a little past the body.
    return {obstacle.texelX - size.halfWide - kWheelRoom, obstacle.texelY - size.spriteTop - size.shear, obstacle.texelX + size.halfWide + kWheelRoom + 1,
            obstacle.texelY + size.rowsBelow + size.shear + 2};
}

ObstacleList::ObstacleList(int tileX, int tileY, Obstacle* out, int capacity)
    : left_(static_cast<float>(tileX) * kChunkWorldSize - kObstacleReachWorld),
      top_(static_cast<float>(tileY) * kChunkWorldSize - kObstacleReachWorld),
      right_(static_cast<float>(tileX + 1) * kChunkWorldSize + kObstacleReachWorld),
      bottom_(static_cast<float>(tileY + 1) * kChunkWorldSize + kObstacleReachWorld),
      out_(out),
      capacity_(capacity) {}

bool ObstacleList::touches(const Obstacle& obstacle) const {
    return obstacle.center.x + obstacle.half.x >= left_ && obstacle.center.x - obstacle.half.x <= right_ && obstacle.center.y + obstacle.half.y >= top_ &&
           obstacle.center.y - obstacle.half.y <= bottom_;
}

void ObstacleList::add(const Obstacle& obstacle) {
    if (!touches(obstacle) || count_ >= capacity_) return;
    out_[count_++] = obstacle;
}

int obstaclesInTile(MapId map, uint64_t seed, int tileX, int tileY, Obstacle* out, int capacity) {
    ObstacleList list(tileX, tileY, out, capacity);
    switch (map) {
        case MapId::Wasteland: wasteland::collectObstacles(seed, tileX, tileY, list); break;
        case MapId::City:
        case MapId::Count: city::collectObstacles(seed, tileX, tileY, list); break;
    }
    return list.count();
}

}  // namespace ls::maps
