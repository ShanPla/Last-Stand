#pragma once

#include "core/Math.h"

#include "raylib.h"

#include <cstdint>

// The ground under the fight. Maps are decoration only: nothing here ever reaches the
// simulation, and everyone walks straight across every prop. A map paints square
// chunks of ground on request, from a seed, so the same run always shows the same
// place. Adding a map means adding one enum value and one case in Maps.cpp.
namespace ls::maps {

enum class MapId : uint8_t { City, Count };

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

}  // namespace ls::maps
