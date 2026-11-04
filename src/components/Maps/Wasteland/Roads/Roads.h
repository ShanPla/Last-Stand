#pragma once

#include "components/Maps/Maps.h"
#include "components/Maps/Paint/Paint.h"
#include "components/Maps/SharedProps/SharedProps.h"
#include "components/Maps/Wasteland/Terrain/Terrain.h"

// The broken highways. There are two families of long roads, one running across the
// screen and one running up it, spaced far apart. Each road is a function: give it a
// position along the road, and it says where its center is and how buried it is. Any
// chunk can ask about any road, so nothing about the network has to be stored.
namespace ls::maps::wasteland {

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

void paintRoads(Paint& paint, const Desert& desert, LightList& lights);

}  // namespace ls::maps::wasteland
