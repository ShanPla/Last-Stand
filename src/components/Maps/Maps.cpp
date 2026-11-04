#include "components/Maps/Maps.h"

#include "components/Maps/City/City.h"
#include "data/Theme.h"

namespace ls::maps {

int renderChunk(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights) {
    switch (map) {
        case MapId::City: return city::renderChunk(seed, chunkX, chunkY, pixels, lights);
        case MapId::Count: break;
    }
    return 0;
}

Color fallbackGround(MapId map) {
    switch (map) {
        case MapId::City: return theme::kAsphalt[0];
        case MapId::Count: break;
    }
    return theme::kBackground;
}

}  // namespace ls::maps
