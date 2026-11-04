#pragma once

#include "raylib.h"

#include <cstdint>

// Drawing tools shared by the maps. All coordinates are global texels of the whole map, not
// local to a chunk, and everything clips to the chunk being painted. That is what
// makes seams invisible: a car over a chunk edge is simply drawn by both chunks.
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

class Paint {
public:
    Paint(Color* pixels, int originX, int originY, int width, int height)
        : pixels_(pixels), originX_(originX), originY_(originY), width_(width), height_(height) {}

    int left() const { return originX_; }
    int top() const { return originY_; }
    int right() const { return originX_ + width_; }
    int bottom() const { return originY_ + height_; }

    bool overlaps(int x0, int y0, int x1, int y1) const { return x1 >= left() && x0 < right() && y1 >= top() && y0 < bottom(); }

    void put(int x, int y, Color color) {
        const int lx = x - originX_;
        const int ly = y - originY_;
        if (lx < 0 || ly < 0 || lx >= width_ || ly >= height_) return;
        pixels_[ly * width_ + lx] = color;
    }

    // Mixes a see-through color over what is already there.
    void blend(int x, int y, Color color) {
        const int lx = x - originX_;
        const int ly = y - originY_;
        if (lx < 0 || ly < 0 || lx >= width_ || ly >= height_) return;
        Color& to = pixels_[ly * width_ + lx];
        const int a = color.a;
        to.r = static_cast<unsigned char>((color.r * a + to.r * (255 - a)) / 255);
        to.g = static_cast<unsigned char>((color.g * a + to.g * (255 - a)) / 255);
        to.b = static_cast<unsigned char>((color.b * a + to.b * (255 - a)) / 255);
    }

    // A rectangle from (x0, y0) up to but not including (x1, y1).
    void rect(int x0, int y0, int x1, int y1, Color color) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) put(x, y, color);
        }
    }

    void blendRect(int x0, int y0, int x1, int y1, Color color) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) blend(x, y, color);
        }
    }

    void line(int x0, int y0, int x1, int y1, Color color) {
        int dx = x1 > x0 ? x1 - x0 : x0 - x1;
        int dy = y1 > y0 ? y0 - y1 : y1 - y0;
        const int stepX = x0 < x1 ? 1 : -1;
        const int stepY = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true) {
            put(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            const int twice = 2 * error;
            if (twice >= dy) {
                error += dy;
                x0 += stepX;
            }
            if (twice <= dx) {
                error += dx;
                y0 += stepY;
            }
        }
    }

    // A filled ellipse, flattened to sit on the ground.
    void oval(int cx, int cy, int rx, int ry, Color color) {
        for (int y = -ry; y <= ry; ++y) {
            for (int x = -rx; x <= rx; ++x) {
                if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry) put(cx + x, cy + y, color);
            }
        }
    }

    void blendOval(int cx, int cy, int rx, int ry, Color color) {
        for (int y = -ry; y <= ry; ++y) {
            for (int x = -rx; x <= rx; ++x) {
                if (x * x * ry * ry + y * y * rx * rx <= rx * rx * ry * ry) blend(cx + x, cy + y, color);
            }
        }
    }

private:
    Color* pixels_;
    int originX_;
    int originY_;
    int width_;
    int height_;
};

}  // namespace ls::maps
