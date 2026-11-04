#pragma once

#include "core/Math.h"

#include "raylib.h"

#include <cstdint>

// The ground under the fight. Maps are decoration only: nothing here ever reaches the
// simulation, and everyone walks straight across every prop. A map paints square
// chunks of ground on request, from a seed, so the same run always shows the same
// place. Adding a map means adding one enum value and one case in each switch in Maps.cpp.
namespace ls::maps {

enum class MapId : uint8_t { City, Wasteland, Count };

// What a new player gets, and what any unknown id falls back to.
inline constexpr MapId kDefaultMap = MapId::City;
inline constexpr int kMapCount = static_cast<int>(MapId::Count);

// The map being shown. The renderer notices a change and repaints its chunks by itself.
void setCurrent(MapId map);
MapId current();

// Turns a stored or typed number into a map, so a bad number can't reach the painters.
MapId mapFromIndex(int index);

// For menus.
const char* name(MapId map);

// A chunk covers this much of the world. Its pixels already include the camera's
// vertical squash, so at the player's depth one chunk pixel is exactly one art pixel.
inline constexpr float kChunkWorldSize = 640.0f;
inline constexpr int kChunkWidth = 384;
inline constexpr int kChunkHeight = 224;

// Lights the renderer draws live, as they change with the time of day.
enum class LightKind : uint8_t { StreetLamp, Fire };

struct Light {
    Vec2 world;           // On the ground, under the light.
    float height = 0.0f;  // How far the bulb or flame is above that, in chunk pixels.
    LightKind kind = LightKind::StreetLamp;
};

inline constexpr int kMaxLightsPerChunk = 12;

// Paints one chunk into pixels, which holds kChunkWidth by kChunkHeight colors. Writes
// its lights into lights, and returns how many. It uses no global state.
int renderChunk(MapId map, uint64_t seed, int chunkX, int chunkY, Color* pixels, Light* lights);

// What to show if a chunk is somehow not ready yet.
Color fallbackGround(MapId map);

// How many ticks one swell of a fire's light takes on this map, and how long its flame
// holds each picture. Bigger numbers are calmer.
uint32_t fireFlickerTicks(MapId map);
uint32_t fireFrameTicks(MapId map);

}  // namespace ls::maps
