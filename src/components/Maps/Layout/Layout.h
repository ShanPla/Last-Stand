#pragma once

#include "components/Maps/Layout/TileRandom/TileRandom.h"
#include "core/Math.h"

#include <cstdint>

// Where a map puts the things that matter to the fight, worked out from the seed alone. This
// is the one part of the maps that the simulation can see, so it uses no graphics library.
// The painters read the same answers, so a solid car is always drawn exactly where it stands.
namespace ls::maps {

enum class MapId : uint8_t { City, Wasteland, Count };

// What a new player gets, and what any unknown id falls back to.
inline constexpr MapId kDefaultMap = MapId::City;
inline constexpr int kMapCount = static_cast<int>(MapId::Count);

// A chunk covers this much of the world. Its pixels already include the camera's
// vertical squash, so at the player's depth one chunk pixel is exactly one art pixel.
inline constexpr float kChunkWorldSize = 640.0f;
inline constexpr int kChunkWidth = 384;
inline constexpr int kChunkHeight = 224;
inline constexpr float kTexelsPerWorldX = static_cast<float>(kChunkWidth) / kChunkWorldSize;
inline constexpr float kTexelsPerWorldY = static_cast<float>(kChunkHeight) / kChunkWorldSize;

// Which picture of a wreck stands here. Each one has its own collision box in Layout.cpp.
enum class CarSprite : uint8_t { CitySide, CityEnd, WreckSide, WreckOnRoof, WreckEnd, BusSide, BusEnd, TankerSide };

// One solid wreck. The simulation reads the box. The renderer also reads the picture's
// place on the map, in chunk texels, so it knows which pixels to treat as the car.
struct Obstacle {
    Vec2 center;
    Vec2 half;
    int texelX = 0;
    int texelY = 0;
    CarSprite sprite = CarSprite::CitySide;
};

// Names one car for as long as a run lasts. Where its feet stand on the map comes from the
// seed alone, so this stays the same however many tile lists the car appears in, and however
// often they are worked out again. Two plain numbers, so it can sit in the game state.
struct CarId {
    int texelX = 0;
    int texelY = 0;
};

inline bool isCar(CarId id, const Obstacle& obstacle) { return id.texelX == obstacle.texelX && id.texelY == obstacle.texelY; }

// A read-only look at the cars the boss has flattened, for the painters and the chunks.
struct CrushedCars {
    const CarId* items = nullptr;
    int count = 0;

    bool holds(int texelX, int texelY) const {
        for (int i = 0; i < count; ++i) {
            if (items[i].texelX == texelX && items[i].texelY == texelY) return true;
        }
        return false;
    }
};

struct TexelRect {
    int x0, y0, x1, y1;  // From (x0, y0) up to but not including (x1, y1).
};

// A car a painter should draw, with the random stream it takes its looks from. It is decided
// by the layout, so the box and the picture can't disagree. A dropped car has exists false.
struct CarPlan {
    bool exists = false;
    int x = 0;
    int y = 0;
    TileRandom looks{0, 0, 0, 0};
};

Obstacle makeObstacle(CarSprite sprite, int texelX, int texelY);

// False for a car too close to where the player starts, so a run never opens against one.
// Both the painters and the simulation drop that car.
bool clearOfStart(const Obstacle& obstacle);

// The part of the map a car's picture covers, in chunk texels.
TexelRect spriteRect(const Obstacle& obstacle);

// Which tile of the map a world position is in. A tile is exactly one painted chunk.
inline int tileOf(float world) { return static_cast<int>(std::floor(world / kChunkWorldSize)); }

// A tile's list holds every car within this distance of the tile. So a circle up to this
// wide around any point can be checked against the single tile holding that point.
inline constexpr float kObstacleReachWorld = 64.0f;

// The most cars a tile can list. A unit test samples 4,000 tiles on 200 seeds and both maps,
// and the busiest holds far fewer, so this limit never decides what is solid.
inline constexpr int kMaxObstaclesPerTile = 32;

// Fills out with every car near this tile and returns how many. It depends only on its
// arguments, which is why replays stay exact: nothing about the map is ever stored.
int obstaclesInTile(MapId map, uint64_t seed, int tileX, int tileY, Obstacle* out, int capacity);

// Collects the cars that touch one tile. Used by each map's layout.
class ObstacleList {
public:
    ObstacleList(int tileX, int tileY, Obstacle* out, int capacity);
    // True when a car's box reaches the tile.
    bool touches(const Obstacle& obstacle) const;
    void add(const Obstacle& obstacle);
    int count() const { return count_; }

private:
    float left_, top_, right_, bottom_;
    Obstacle* out_;
    int capacity_;
    int count_ = 0;
};

}  // namespace ls::maps
