#pragma once

#include "components/Maps/Layout/Layout.h"
#include "core/Math.h"

#include "raylib.h"

#include <cstdint>

// The ground under the fight. A map paints square chunks of ground on request, from a seed,
// so the same run always shows the same place. Its props are decoration and everyone walks
// across them. Only its cars are solid, and those come from the Layout folder, which is the
// one part of a map the simulation can see. Adding a map means adding one enum value in
// Layout.h, one case in each switch in Maps.cpp, and its layout in the Layout folder.
namespace ls::maps {

// Turns a stored or typed number into a map, so a bad number can't reach the painters.
MapId mapFromIndex(int index);

// For menus.
const char* name(MapId map);

// Lights the renderer draws live, as they change with the time of day.
enum class LightKind : uint8_t { StreetLamp, Fire };

struct Light {
    Vec2 world;           // On the ground, under the light.
    float height = 0.0f;  // How far the bulb or flame is above that, in chunk pixels.
    LightKind kind = LightKind::StreetLamp;
};

inline constexpr int kMaxLightsPerChunk = 12;

// Paints one chunk into pixels, which holds kChunkWidth by kChunkHeight colors. Writes
// its lights into lights, and returns how many. It uses no global state, so the same seed
// and the same crushed cars always give the same picture.
int renderChunk(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights, CrushedCars crushed = {});

// The same painting in two stages, so it can be spread over several frames on a slow device.
// First the bare ground, in any bands of rows, which must cover the chunk from top to bottom.
// Then the details over it. The result is exactly what renderChunk paints in one go.
void paintGroundRows(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, int rowFrom, int rowTo);
int paintDetails(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights, CrushedCars crushed = {});

// What to show if a chunk is somehow not ready yet.
Color fallbackGround(MapId map);

// How many ticks one swell of a fire's light takes on this map, and how long its flame
// holds each picture. Bigger numbers are calmer.
uint32_t fireFlickerTicks(MapId map);
uint32_t fireFrameTicks(MapId map);

}  // namespace ls::maps
