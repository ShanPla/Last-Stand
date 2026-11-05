#pragma once

#include <cstdint>

// The small hashes every map decision is made from. They use no graphics library, so the
// simulation can ask the same questions the painters ask, and get the same answers.
namespace ls::maps {

inline uint32_t mix(uint32_t value) {
    value ^= value >> 15;
    value *= 0x2C1B3C6Du;
    value ^= value >> 12;
    value *= 0x297A2D39u;
    value ^= value >> 15;
    return value;
}

// A cheap hash of one texel, for speckle. Called for every pixel of a chunk.
inline uint32_t texelNoise(int x, int y) {
    const uint32_t value = (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u);
    return (value * 0x9E3779B1u) >> 16;
}

// Smooth noise from 0 to 255, for large soft patches of wear. Corners of a coarse grid
// get a hash, and each texel blends between its four corners. Hard coarse squares
// would look like a checkerboard laid over the map.
inline int softNoise(int x, int y, int shiftX, int shiftY) {
    const int cellX = x >> shiftX;
    const int cellY = y >> shiftY;
    const int sizeX = 1 << shiftX;
    const int sizeY = 1 << shiftY;
    const int fx = x & (sizeX - 1);
    const int fy = y & (sizeY - 1);
    const int a = static_cast<int>(texelNoise(cellX, cellY) & 255u);
    const int b = static_cast<int>(texelNoise(cellX + 1, cellY) & 255u);
    const int c = static_cast<int>(texelNoise(cellX, cellY + 1) & 255u);
    const int d = static_cast<int>(texelNoise(cellX + 1, cellY + 1) & 255u);
    const int top = a * (sizeX - fx) + b * fx;
    const int bottom = c * (sizeX - fx) + d * fx;
    return (top * (sizeY - fy) + bottom * fy) >> (shiftX + shiftY);
}

// A small repeatable random number stream, seeded per tile.
class TileRandom {
public:
    TileRandom(uint64_t seed, int a, int b, uint32_t salt) {
        state_ = mix(static_cast<uint32_t>(seed) ^ mix(static_cast<uint32_t>(seed >> 32) + salt) ^ mix(static_cast<uint32_t>(a) * 0x9E3779B1u) ^
                     mix(static_cast<uint32_t>(b) * 0x85EBCA6Bu + 0x1234567u));
    }

    uint32_t next() {
        state_ = state_ * 1664525u + 1013904223u;
        return mix(state_);
    }
    // From low up to and including high.
    int range(int low, int high) { return high <= low ? low : low + static_cast<int>(next() % static_cast<uint32_t>(high - low + 1)); }
    bool chance(int percent) { return static_cast<int>(next() % 100u) < percent; }

private:
    uint32_t state_;
};

}  // namespace ls::maps
