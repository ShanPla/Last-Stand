#include "components/Maps/Maps.h"

#include "components/Maps/City/City.h"
#include "components/Maps/Wasteland/Wasteland.h"
#include "data/Theme.h"

namespace ls::maps {

namespace {

MapId g_current = kDefaultMap;

}  // namespace

void setCurrent(MapId map) { g_current = mapFromIndex(static_cast<int>(map)); }

MapId current() { return g_current; }

MapId mapFromIndex(int index) {
    if (index < 0 || index >= kMapCount) return kDefaultMap;
    return static_cast<MapId>(index);
}

const char* name(MapId map) {
    switch (map) {
        case MapId::City: return "Ruined City";
        case MapId::Wasteland: return "Desert Highway";
        case MapId::Count: break;
    }
    return name(kDefaultMap);
}

int renderChunk(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights) {
    switch (map) {
        case MapId::City: return city::renderChunk(seed, chunkX, chunkY, pixels, lights);
        case MapId::Wasteland: return wasteland::renderChunk(seed, chunkX, chunkY, pixels, lights);
        case MapId::Count: break;
    }
    return city::renderChunk(seed, chunkX, chunkY, pixels, lights);
}

Color fallbackGround(MapId map) {
    switch (map) {
        case MapId::City: return theme::kAsphalt[0];
        case MapId::Wasteland: return theme::kSand[0];
        case MapId::Count: break;
    }
    return theme::kAsphalt[0];
}

uint32_t fireFlickerTicks(MapId map) {
    // The desert's fires swell slowly: at night they are the brightest thing around.
    return map == MapId::Wasteland ? 90u : 7u;
}

uint32_t fireFrameTicks(MapId map) { return map == MapId::Wasteland ? 9u : 5u; }

}  // namespace ls::maps
