#pragma once

#include "core/Math.h"

#include <cstdint>
#include <cstring>

namespace ls {

// Finds nearby objects without checking every object. The world is split into
// square cells, and a query only looks at the cells it overlaps. This makes
// thousands of collision checks per frame affordable.
//
// Cell coordinates wrap around at the grid size, so an endless world needs no
// resizing. Objects stay close to the player, far less than one grid span, so
// two objects in the same wrapped cell are really near each other.
//
// Rebuilt with a counting sort, which is O(n) and allocates nothing.
template <int MaxItems>
class SpatialHash {
public:
    static constexpr int kGrid = 128;  // Must be a power of two.
    static constexpr float kCellSize = 64.0f;

    template <typename Item>
    void build(const Item* items, int count) {
        std::memset(cellEnd_, 0, sizeof(cellEnd_));
        for (int i = 0; i < count; ++i) cellEnd_[cellIndex(items[i].pos)]++;
        int total = 0;
        for (int cell = 0; cell < kGrid * kGrid; ++cell) {
            total += cellEnd_[cell];
            cellEnd_[cell] = total;
        }
        // Filled from the back, so each cellEnd_ becomes that cell's start.
        for (int i = count - 1; i >= 0; --i) {
            entries_[--cellEnd_[cellIndex(items[i].pos)]] = static_cast<uint16_t>(i);
        }
        cellEnd_[kGrid * kGrid] = total;
        count_ = total;
    }

    // Calls visit(itemIndex) for each item in cells overlapping the circle.
    // Results are candidates: the caller does the exact distance check.
    // If visit returns true, the query stops early.
    template <typename Visit>
    void query(Vec2 center, float radius, Visit&& visit) const {
        const int minX = cellCoord(center.x - radius);
        const int maxX = cellCoord(center.x + radius);
        const int minY = cellCoord(center.y - radius);
        const int maxY = cellCoord(center.y + radius);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const int cell = wrappedCell(x, y);
                const int end = cell + 1 < kGrid * kGrid ? cellEnd_[cell + 1] : count_;
                for (int i = cellEnd_[cell]; i < end; ++i) {
                    if (visit(static_cast<int>(entries_[i]))) return;
                }
            }
        }
    }

private:
    static int cellCoord(float value) { return static_cast<int>(std::floor(value / kCellSize)); }
    static int wrappedCell(int x, int y) { return (x & (kGrid - 1)) + ((y & (kGrid - 1)) * kGrid); }
    static int cellIndex(Vec2 pos) { return wrappedCell(cellCoord(pos.x), cellCoord(pos.y)); }

    int32_t cellEnd_[kGrid * kGrid + 1] = {};
    uint16_t entries_[MaxItems] = {};
    int count_ = 0;
};

}  // namespace ls
