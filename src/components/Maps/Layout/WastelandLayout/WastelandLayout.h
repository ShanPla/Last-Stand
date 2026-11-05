#pragma once

#include "components/Maps/Layout/Layout.h"

// The geography of the desert: what kind of land is where, where the highways run, and
// where the wrecks stand. It holds no pixels. The painters in Maps/Wasteland and the
// simulation both read it, so they always agree about where a wreck is.
namespace ls::maps::wasteland {

// Division that rounds down, so that negative map coordinates work like positive ones.
inline int floorDiv(int a, int b) {
    const int q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}
inline int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }

// --- The land -----------------------------------------------------------------

enum class Zone : uint8_t { Dunes, Sand, Pan, Reg, Scrub, RedEarth };

// The seed slides the large noise fields, so that each run gets its own geography.
struct Desert {
    uint64_t seed;
    int ax, ay, bx, by, windX, windY;
};

Desert desertFor(uint64_t seed);

// Most of the desert stays open sand. The two fields cut it into broad regions.
inline constexpr int kDunesAbove = 146;
inline constexpr int kFlatsBelow = 108;

int fieldA(const Desert& desert, int x, int y);
int fieldB(const Desert& desert, int x, int y);
Zone classifyZone(int a, int b);

// What kind of land is at a texel. Depends only on the seed and the place.
Zone zoneAt(const Desert& desert, int x, int y);

// --- The highways -------------------------------------------------------------

// There are two families of long roads, one running across the screen and one running up it,
// spaced far apart. Each road is a function: give it a position along the road, and it says
// where its center is and how buried it is. Any chunk can ask about any road, so nothing
// about the network has to be stored.
struct RoadFamily {
    bool across;       // True: runs along x, and its center is a y.
    uint32_t salt;
    int spacing;       // Average distance between neighboring roads.
    int jitter;        // How far a road strays from its slot.
    int step;          // Bends and sand drifts change over one step like this.
    int reach;         // The furthest a bend moves the road from its base line.
    int halfWidth;     // Half the width of two lanes.
    int dividedHalf;   // Half the width of a road with a median.
    int median;        // Half the width of that median.
    int shoulder;      // Gravel beyond the edge.
};

inline constexpr RoadFamily kAcrossRoads{true, 101, 1568, 300, 1536, 300, 27, 45, 7, 8};
inline constexpr RoadFamily kUpRoads{false, 211, 3456, 800, 896, 480, 46, 76, 12, 13};

// Added to a family's salt, so its random choices stay independent of each other.
enum RoadSalt : uint32_t { kSaltBase = 0, kSaltBend = 1, kSaltBurial = 2, kSaltStyle = 3, kSaltDamage = 4, kSaltFurniture = 5, kSaltStretch = 6, kSaltLine = 7, kSaltCrossing = 8 };

struct RoadStyle {
    int half;
    int centerLine;  // 0 dashes, 1 double line, 2 nearly gone.
    bool divided;
    bool poles;
    bool polesOnLowSide;
};

RoadStyle styleOf(uint64_t seed, const RoadFamily& family, int index);

int roadBase(uint64_t seed, const RoadFamily& family, int index);
int roadCenter(uint64_t seed, const RoadFamily& family, int index, int along);
// 0 is clear road, and 255 is gone under the sand.
int roadBurial(uint64_t seed, const RoadFamily& family, int index, int along);
int roadHalfWidth(uint64_t seed, const RoadFamily& family, int index);

// The indexes of the roads that could come within reach of a span of coordinates.
inline int firstRoadNear(const RoadFamily& family, int low) { return floorDiv(low - family.jitter - family.reach, family.spacing); }
inline int lastRoadNear(const RoadFamily& family, int high) { return floorDiv(high + family.jitter + family.reach, family.spacing) + 1; }

// True if a road of either family, buried or not, passes within these margins of a point.
bool nearAnyRoad(uint64_t seed, int x, int y, int marginX, int marginY);

struct RoadPoint {
    int along, center, x, y;
};

RoadPoint roadPoint(uint64_t seed, const RoadFamily& family, int index, int along, int off);

// Roads are cut into slots along their length. Each slot rolls its own wear and its own
// roadside objects from a hash, so any chunk can repaint exactly what its neighbor did.
inline int slotLength(const RoadFamily& family) { return family.across ? 64 : 36; }

// The longest wreck on a road, in texels along it. A slot this far off can still reach a tile.
inline constexpr int kWreckReachAlong = 130;

// What a slot of road holds on the road itself. The stream carries on from here: the painter
// takes the looks of a wreck, or the skid marks, from the same stream.
struct RoadSlot {
    bool wreck = false;
    bool skidMarks = false;  // No wreck, but somebody braked hard here.
    int along = 0;
    int x = 0;
    int y = 0;
    CarSprite sprite = CarSprite::WreckSide;
};

RoadSlot decideRoadSlot(uint64_t seed, const RoadFamily& family, int index, const RoadStyle& style, int slot, TileRandom& random);

// The stream that decides a slot. layer 1 is the road itself, 0 and 2 the two roadsides.
inline TileRandom slotStream(uint64_t seed, const RoadFamily& family, int index, int slot, int layer) {
    return TileRandom(seed, index, slot, family.salt + kSaltFurniture + static_cast<uint32_t>(layer) * 16u);
}

// --- Sites --------------------------------------------------------------------

// The places worth finding on a long walk. Each is placed by a hash of a coarse grid cell.
enum class SiteKind { None, Shacks, Rocks, RedRocks, Grove, Bones, Wrecks, Foundations, Camp };

inline constexpr int kSiteReachX = 260;  // Sites sit inside this box around their center.
inline constexpr int kSiteReachY = 150;
inline constexpr int kCellWidth = kChunkWidth * 3;
inline constexpr int kCellHeight = kChunkHeight * 3;

enum SiteSalt : uint32_t { kSaltCell = 401, kSaltRoadside = 409, kSaltPaint = 419, kSaltGasCar = 431 };

struct SiteSpot {
    int x, y;
    int roll;
};

SiteSpot siteSpot(uint64_t seed, int i, int j);
SiteKind pickSite(Zone zone, int roll);
// What stands at this spot, or None if a road runs through it: roads win.
SiteKind siteKindAt(const Desert& desert, const SiteSpot& spot);

// Objects in a cluster are placed first and drawn after sorting by row, so the ones
// nearer the camera always overlap the ones behind them.
struct Placed {
    int x, y, kind;
};
inline constexpr int kMaxPlaced = 24;
void sortByRow(Placed* items, int count);

// A wreck yard's cars and tire piles are the first thing its stream decides.
int planWreckYard(const SiteSpot& spot, TileRandom& random, Placed* items);
// Which picture a wreck yard's item shows. A tire pile is not solid, so it returns false.
bool wreckYardSprite(int kind, CarSprite* sprite);

// Highway stops: gas stations mostly, a few shack clusters. Only on straight, clear road.
inline constexpr int kStopWidth = 280;
inline constexpr int kStopHeight = 150;

inline int stopStretchLength(const RoadFamily& family) { return family.across ? kChunkWidth * 6 : kChunkHeight * 6; }

struct RoadStop {
    bool exists = false;
    bool gas = false;
    int x0 = 0;
    int y0 = 0;
};

RoadStop roadStopAt(uint64_t seed, const RoadFamily& family, int index, int stretch);

// The car left on a gas station's forecourt, if any.
CarPlan gasStationCar(uint64_t seed, const RoadFamily& family, int index, int stretch, const RoadStop& stop);

void collectObstacles(uint64_t seed, int tileX, int tileY, ObstacleList& list);

}  // namespace ls::maps::wasteland
