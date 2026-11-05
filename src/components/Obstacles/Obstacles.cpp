#include "components/Obstacles/Obstacles.h"

#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ls::obstacles {

namespace {

using maps::Obstacle;

// Each tile of the map has one fixed slot, picked by its position modulo the window's size.
// So when the player moves, tiles that stay in the window stay in their slots, and nothing
// has to be copied: only the slots whose tile has changed get filled again.
int slotOf(int tileX, int tileY) {
    const int x = ((tileX % kObstacleWindow) + kObstacleWindow) % kObstacleWindow;
    const int y = ((tileY % kObstacleWindow) + kObstacleWindow) % kObstacleWindow;
    return x + y * kObstacleWindow;
}

// The list of cars near a point, or nullptr when the point is outside the window. Off in the
// distance nothing can see the difference, so a zombie out there simply walks on. Every point
// outside the window fails the same way whatever the player did before, which keeps replays exact.
const ObstacleTile* tileAt(const GameState& state, Vec2 pos) {
    const int tileX = maps::tileOf(pos.x);
    const int tileY = maps::tileOf(pos.y);
    const ObstacleTile& tile = state.obstacleCache.tiles[slotOf(tileX, tileY)];
    return tile.valid && tile.tileX == tileX && tile.tileY == tileY ? &tile : nullptr;
}

// Pushes a circle out of one box. The box is shrunk by the corner radius and the circle grows
// by the same amount, which is the same test as a box with rounded corners.
bool pushOutOf(const Obstacle& box, Vec2& pos, float radius, Vec2* normal) {
    const float corner = balance::kCarCornerRadius;
    const float reach = radius + corner;
    const Vec2 fromCenter = pos - box.center;
    const float innerX = box.half.x - corner;
    const float innerY = box.half.y - corner;
    if (std::abs(fromCenter.x) >= innerX + reach || std::abs(fromCenter.y) >= innerY + reach) return false;
    const Vec2 nearest{clampf(fromCenter.x, -innerX, innerX), clampf(fromCenter.y, -innerY, innerY)};
    const Vec2 away = fromCenter - nearest;
    const float distSq = lengthSq(away);
    if (distSq >= reach * reach) return false;
    if (distSq > 1e-6f) {
        const float dist = std::sqrt(distSq);
        *normal = away * (1.0f / dist);
        pos += *normal * (reach - dist);
        return true;
    }
    // The center is inside the box: leave by the nearest side.
    const float outX = innerX - std::abs(fromCenter.x) + reach;
    const float outY = innerY - std::abs(fromCenter.y) + reach;
    if (outX < outY) {
        *normal = Vec2{fromCenter.x < 0.0f ? -1.0f : 1.0f, 0.0f};
        pos.x += normal->x * outX;
    } else {
        *normal = Vec2{0.0f, fromCenter.y < 0.0f ? -1.0f : 1.0f};
        pos.y += normal->y * outY;
    }
    return true;
}

constexpr float kStuckSearch = 200.0f;     // A stuck walker looks this far for the car holding it.
constexpr float kCornerClearance = 3.0f;  // Heads a little wide of a corner, so it doesn't clip it.
constexpr float kFitSlack = 0.25f;        // How much overlap still counts as having fit.
constexpr float kLineSlack = 1.0f;        // A line running along a car's side counts as clear of it.

// True when a straight line between two points doesn't enter a box centered on (0, 0). It trims
// the line to the slab between the box's left and right, then to the slab between its top
// and bottom. If anything of the line is left, it passes through the box.
bool lineMissesBox(Vec2 from, Vec2 to, float halfX, float halfY) {
    float enter = 0.0f;
    float leave = 1.0f;
    const auto trim = [&](float start, float delta, float half) {
        if (std::abs(delta) < 1e-6f) return std::abs(start) < half;
        float a = (-half - start) / delta;
        float b = (half - start) / delta;
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
        return enter < leave;
    };
    return !(trim(from.x, to.x - from.x, halfX) && trim(from.y, to.y - from.y, halfY));
}

// The first car ahead, in a box grown by the walker's radius.
const Obstacle* carAhead(const ObstacleTile& tile, Vec2 probe, float radius) {
    for (int i = 0; i < tile.count; ++i) {
        const Obstacle& box = tile.items[i];
        if (std::abs(probe.x - box.center.x) < box.half.x + radius && std::abs(probe.y - box.center.y) < box.half.y + radius) return &box;
    }
    return nullptr;
}

// Picks where to walk to get past a box on the way to a target, working in the box's own
// coordinates, with the box grown by the walker's radius so that the walker is just a point.
// Returns false when the straight line to the target is clear after all.
bool wayAround(const Obstacle& box, Vec2 pos, float radius, Vec2 targetPos, uint32_t side, Vec2* corner) {
    const float solidX = box.half.x + radius - kLineSlack;
    const float solidY = box.half.y + radius - kLineSlack;
    const float reachX = box.half.x + radius + kCornerClearance;
    const float reachY = box.half.y + radius + kCornerClearance;
    const Vec2 from = pos - box.center;
    Vec2 to = targetPos - box.center;
    // A target standing right beside the car is "inside" the grown box. The best anyone
    // can do is reach the edge nearest to it.
    if (std::abs(to.x) < solidX && std::abs(to.y) < solidY) {
        if (solidX - std::abs(to.x) < solidY - std::abs(to.y)) to.x = to.x < 0.0f ? -solidX : solidX;
        else to.y = to.y < 0.0f ? -solidY : solidY;
    }
    if (lineMissesBox(from, to, solidX, solidY)) return false;

    // Of the corners in view, the one on the shortest way to the target. As a walker moves
    // toward a corner that way only gets shorter, so it never changes its mind halfway along
    // the car and dithers in the middle. A corner is in view from past either of its two sides.
    float best = 1.0e30f;
    float second = 1.0e30f;
    Vec2 bestCorner = from;
    Vec2 secondCorner = from;
    for (const float signX : {-1.0f, 1.0f}) {
        for (const float signY : {-1.0f, 1.0f}) {
            if (from.x * signX < box.half.x && from.y * signY < box.half.y) continue;
            const Vec2 at{signX * reachX, signY * reachY};
            // From that corner the rest of the way may still be blocked by the car itself.
            // Then it has to pass one of the two corners next to it as well.
            float rest = distance(at, to);
            if (!lineMissesBox(at, to, solidX, solidY)) {
                const Vec2 alongX{-at.x, at.y};
                const Vec2 alongY{at.x, -at.y};
                rest = std::min(distance(at, alongX) + distance(alongX, to), distance(at, alongY) + distance(alongY, to));
            }
            const float way = distance(from, at) + rest;
            if (way < best) {
                second = best;
                secondCorner = bestCorner;
                best = way;
                bestCorner = at;
            } else if (way < second) {
                second = way;
                secondCorner = at;
            }
        }
    }
    // Exactly opposite the target both ways are equal, and the id splits a pack down the middle.
    const bool tie = second - best < 1.0f && (side & 1u) != 0u;
    *corner = box.center + (tie ? secondCorner : bestCorner);
    return true;
}

}  // namespace

void invalidate(GameState& state) {
    for (ObstacleTile& tile : state.obstacleCache.tiles) tile.valid = false;
    state.obstacleCache.ready = false;
}

void update(GameState& state) {
    ObstacleCache& cache = state.obstacleCache;
    const int centerX = maps::tileOf(state.player.pos.x);
    const int centerY = maps::tileOf(state.player.pos.y);
    if (cache.ready && cache.centerX == centerX && cache.centerY == centerY) return;
    cache.ready = true;
    cache.centerX = centerX;
    cache.centerY = centerY;
    const int reach = kObstacleWindow / 2;
    for (int y = centerY - reach; y <= centerY + reach; ++y) {
        for (int x = centerX - reach; x <= centerX + reach; ++x) {
            ObstacleTile& tile = cache.tiles[slotOf(x, y)];
            if (tile.valid && tile.tileX == x && tile.tileY == y) continue;
            tile.tileX = x;
            tile.tileY = y;
            tile.valid = true;
            tile.count = maps::obstaclesInTile(state.run.map, state.run.seed, x, y, tile.items, maps::kMaxObstaclesPerTile);
        }
    }
}

Push resolveCircle(const GameState& state, Vec2& pos, float radius) {
    Push result;
    // Two rounds, because leaving one car can put the circle into the one beside it.
    for (int round = 0; round < 2; ++round) {
        const ObstacleTile* tile = tileAt(state, pos);
        if (!tile) break;
        bool moved = false;
        for (int i = 0; i < tile->count; ++i) {
            if (pushOutOf(tile->items[i], pos, radius, &result.normal)) moved = true;
        }
        if (!moved) break;
        result.hit = true;
    }
    return result;
}

Vec2 sweepCircle(const GameState& state, Vec2 from, Vec2 to, float radius) {
    const Vec2 delta = to - from;
    const float distance = length(delta);
    const int steps = std::max(1, static_cast<int>(std::ceil(distance / std::max(radius, 1.0f))));
    const Vec2 stepBy = delta * (1.0f / static_cast<float>(steps));
    Vec2 pos = from;
    for (int i = 0; i < steps; ++i) {
        const Vec2 before = pos;
        pos += stepBy;
        // Two cars can stand too close to fit between. Then it just stops where it was.
        if (resolveCircle(state, pos, radius).hit && overlapsCar(state, pos, radius - kFitSlack)) return before;
    }
    return pos;
}

Vec2 steerAround(const GameState& state, Vec2 pos, float radius, Vec2 velocity, Vec2 targetPos, uint32_t side) {
    const float speedSq = lengthSq(velocity);
    if (speedSq < 1e-4f) return velocity;
    const float speed = std::sqrt(speedSq);
    const float lookAhead = radius + balance::kCarSteerLookAhead;
    // The target is closer than a car could be in the way, so there is nothing to go around.
    if (distanceSq(pos, targetPos) <= lookAhead * lookAhead) return velocity;
    const ObstacleTile* tile = tileAt(state, pos + velocity * (lookAhead / speed));
    if (!tile || tile->count == 0) return velocity;
    const Obstacle* box = carAhead(*tile, pos + velocity * (lookAhead / speed), radius);
    if (!box) return velocity;

    // Going around one car can lead straight into the one parked beside it. So look along the
    // new heading once more, and if another car is there, go around that one on the way.
    Vec2 target = targetPos;
    Vec2 heading = velocity * (1.0f / speed);
    for (int look = 0; look < 2; ++look) {
        Vec2 corner;
        if (!wayAround(*box, pos, radius, target, side, &corner)) return look == 0 ? velocity : heading * speed;
        heading = normalizedOr(corner - pos, heading);
        target = corner;
        const Obstacle* next = carAhead(*tile, pos + heading * lookAhead, radius);
        if (!next || next == box) break;
        box = next;
    }
    return heading * speed;
}

bool overlapsCar(const GameState& state, Vec2 pos, float radius) {
    const ObstacleTile* tile = tileAt(state, pos);
    if (!tile) return false;
    Vec2 copy = pos;
    Vec2 normal;
    for (int i = 0; i < tile->count; ++i) {
        if (pushOutOf(tile->items[i], copy, radius, &normal)) return true;
    }
    return false;
}

float distanceToNearestCar(const GameState& state, Vec2 pos, float limit) {
    const ObstacleTile* tile = tileAt(state, pos);
    if (!tile) return limit;
    float best = limit;
    for (int i = 0; i < tile->count; ++i) {
        const Obstacle& box = tile->items[i];
        const float gapX = std::max(std::abs(pos.x - box.center.x) - box.half.x, 0.0f);
        const float gapY = std::max(std::abs(pos.y - box.center.y) - box.half.y, 0.0f);
        best = std::min(best, std::sqrt(gapX * gapX + gapY * gapY));
    }
    return best;
}

bool lineBlocked(const GameState& state, Vec2 from, Vec2 to, float radius) {
    // A tile's list reaches past its edges, so looking at the ends and the middle of a line
    // finds every car along it, as long as the line is shorter than a tile.
    const ObstacleTile* tiles[3] = {tileAt(state, from), tileAt(state, (from + to) * 0.5f), tileAt(state, to)};
    for (const ObstacleTile* tile : tiles) {
        if (!tile) continue;
        for (int i = 0; i < tile->count; ++i) {
            const Obstacle& box = tile->items[i];
            if (!lineMissesBox(from - box.center, to - box.center, box.half.x + radius, box.half.y + radius)) return true;
        }
    }
    return false;
}

bool cornerToward(const GameState& state, Vec2 pos, float radius, Vec2 targetPos, Vec2* corner) {
    const ObstacleTile* tile = tileAt(state, pos);
    if (!tile) return false;
    const Obstacle* nearest = nullptr;
    float nearestGap = kStuckSearch;
    for (int i = 0; i < tile->count; ++i) {
        const Obstacle& box = tile->items[i];
        const float gapX = std::max(std::abs(pos.x - box.center.x) - box.half.x, 0.0f);
        const float gapY = std::max(std::abs(pos.y - box.center.y) - box.half.y, 0.0f);
        const float gap = gapX + gapY;
        if (gap < nearestGap) {
            nearestGap = gap;
            nearest = &box;
        }
    }
    if (!nearest) return false;
    const float margin = radius + balance::kCarCornerRadius;
    const Vec2 fromCar = targetPos - nearest->center;
    *corner = nearest->center + Vec2{(fromCar.x < 0.0f ? -1.0f : 1.0f) * (nearest->half.x + margin), (fromCar.y < 0.0f ? -1.0f : 1.0f) * (nearest->half.y + margin)};
    return true;
}

}  // namespace ls::obstacles
