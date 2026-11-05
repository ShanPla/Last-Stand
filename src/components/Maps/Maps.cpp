#include "components/Maps/Maps.h"

#include "components/Maps/City/City.h"
#include "components/Maps/Wasteland/Wasteland.h"
#include "data/Theme.h"

namespace ls::maps {

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

void paintGroundRows(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, int rowFrom, int rowTo) {
    switch (map) {
        case MapId::City: city::paintGroundRows(seed, chunkX, chunkY, pixels, rowFrom, rowTo); return;
        case MapId::Wasteland: wasteland::paintGroundRows(seed, chunkX, chunkY, pixels, rowFrom, rowTo); return;
        case MapId::Count: break;
    }
    city::paintGroundRows(seed, chunkX, chunkY, pixels, rowFrom, rowTo);
}

int paintDetails(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights, CrushedCars crushed) {
    switch (map) {
        case MapId::City: return city::paintDetails(seed, chunkX, chunkY, pixels, lights, crushed);
        case MapId::Wasteland: return wasteland::paintDetails(seed, chunkX, chunkY, pixels, lights, crushed);
        case MapId::Count: break;
    }
    return city::paintDetails(seed, chunkX, chunkY, pixels, lights, crushed);
}

int renderChunk(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights, CrushedCars crushed) {
    paintGroundRows(map, seed, chunkX, chunkY, pixels, 0, kChunkHeight);
    return paintDetails(map, seed, chunkX, chunkY, pixels, lights, crushed);
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
