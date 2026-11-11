#include "components/Obstacles/Obstacles.h"

#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ls::obstacles {

namespace {

using maps::Obstacle;

// Each tile of the map has one fixed slot in a window, picked by its position modulo the
// window's size. So when a player moves, tiles that stay in their window stay in their slots,
// and nothing has to be copied: only the slots whose tile has changed get filled again.
int slotOf(int tileX, int tileY) {
    const int x = ((tileX % kObstacleWindow) + kObstacleWindow) % kObstacleWindow;
    const int y = ((tileY % kObstacleWindow) + kObstacleWindow) % kObstacleWindow;
    return x + y * kObstacleWindow;
}

// A tile in the windows of the first windowCount players, or nullptr. Every window holds
// exactly the same cars for a tile, so it doesn't matter which one answers. A template, so the
// same code serves a cache that may be changed and one that may only be read.
template <class Cache>
auto findTile(Cache& cache, int windowCount, int tileX, int tileY) -> decltype(&cache.windows[0].tiles[0]) {
    const int slot = slotOf(tileX, tileY);
    for (int p = 0; p < windowCount; ++p) {
        auto& tile = cache.windows[p].tiles[slot];
        if (tile.valid && tile.tileX == tileX && tile.tileY == tileY) return &tile;
    }
    return nullptr;
}

// The list of cars near a point, or nullptr when the point is outside every player's window.
// Off in the distance nothing can see the difference, so a zombie out there simply walks on.
// Every point outside the windows fails the same way whatever the players did before, which
// keeps replays exact.
const ObstacleTile* tileAt(const GameState& state, Vec2 pos) {
    return findTile(state.obstacleCache, state.playerCount(), maps::tileOf(pos.x), maps::tileOf(pos.y));
}

// The lists must stay exactly what a fresh look at the map would give, whatever order things
// were crushed in, or the cache could make two replays differ. So a car comes out by closing
// the gap, and never by swapping the last one into its place.
void removeFromTile(ObstacleTile& tile, maps::CarId car) {
    int kept = 0;
    for (int i = 0; i < tile.count; ++i) {
        if (!maps::isCar(car, tile.items[i])) tile.items[kept++] = tile.items[i];
    }
    tile.count = kept;
}

// One car can be in several tiles' lists, because a list reaches past its tile's edges, and
// one tile can be in several players' windows.
void removeFromCache(GameState& state, maps::CarId car) {
    for (ObstacleWindow& window : state.obstacleCache.windows) {
        for (ObstacleTile& tile : window.tiles) {
            if (tile.valid) removeFromTile(tile, car);
        }
    }
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
    const float solidX = box.half.x + radius - balance::kCarLineSlack;
    const float solidY = box.half.y + radius - balance::kCarLineSlack;
    const float reachX = box.half.x + radius + balance::kCarCornerClearance;
    const float reachY = box.half.y + radius + balance::kCarCornerClearance;
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

// True when the player could stand here and walk away in a straight line. Wrecks can close a
// pocket off on all sides, and a pocket like that fits the player but can't be walked into.
bool playerCanWalkTo(const GameState& state, Vec2 spot) {
    if (overlapsCar(state, spot, balance::kPlayerRadius)) return false;
    const float radius = balance::kPlayerRadius - balance::kItemWayOutSlack;
    for (int i = 0; i < balance::kItemWayOutLines; ++i) {
        const Vec2 dir = fromAngle(kTau * static_cast<float>(i) / static_cast<float>(balance::kItemWayOutLines));
        if (!lineBlocked(state, spot, spot + dir * balance::kItemWayOutReach, radius)) return true;
    }
    return false;
}

// True when the player can get close enough to pick up something lying here.
bool collectableAt(const GameState& state, Vec2 spot) {
    if (playerCanWalkTo(state, spot)) return true;
    // A little inside the collect range, because the player's own position is what is measured.
    const float reach = balance::kPickupCollectRadius - 2.0f;
    for (int i = 0; i < balance::kItemWayOutLines; ++i) {
        const Vec2 dir = fromAngle(kTau * static_cast<float>(i) / static_cast<float>(balance::kItemWayOutLines));
        if (playerCanWalkTo(state, spot + dir * reach)) return true;
    }
    return false;
}

}  // namespace

void invalidate(GameState& state) {
    for (ObstacleWindow& window : state.obstacleCache.windows) {
        for (ObstacleTile& tile : window.tiles) tile.valid = false;
        window.ready = false;
    }
}

// Each player has a window of tiles around them. It costs nothing until that player crosses
// into a new tile, and then only the tiles that came into view are filled.
void update(GameState& state) {
    ObstacleCache& cache = state.obstacleCache;
    const int reach = kObstacleWindow / 2;
    for (int p = 0; p < state.playerCount(); ++p) {
        ObstacleWindow& window = cache.windows[p];
        const int centerX = maps::tileOf(state.players[p].pos.x);
        const int centerY = maps::tileOf(state.players[p].pos.y);
        if (window.ready && window.centerX == centerX && window.centerY == centerY) continue;
        window.ready = true;
        window.centerX = centerX;
        window.centerY = centerY;
        for (int y = centerY - reach; y <= centerY + reach; ++y) {
            for (int x = centerX - reach; x <= centerX + reach; ++x) {
                ObstacleTile& tile = window.tiles[slotOf(x, y)];
                if (tile.valid && tile.tileX == x && tile.tileY == y) continue;
                // A teammate's window may hold this tile already, and it is the same either way.
                // Copying it is cheaper than reading the map again.
                if (const ObstacleTile* copy = findTile(cache, p, x, y)) {
                    tile = *copy;
                    continue;
                }
                tile.tileX = x;
                tile.tileY = y;
                tile.valid = true;
                tile.count = maps::obstaclesInTile(state.run.map, state.run.seed, x, y, tile.items, maps::kMaxObstaclesPerTile);
                // This is the one place a flat car is kept out of what is solid, so walking away
                // and coming back finds it flat. Everything else reads these lists.
                for (int i = 0; i < state.run.crushedCount && tile.count > 0; ++i) removeFromTile(tile, state.run.crushed[i]);
            }
        }
    }
}

// A tile's list holds what is near it, so one tile is enough only for a body this small.
static_assert(balance::kEnemyStats[static_cast<int>(EnemyType::Boss)].radius <= maps::kObstacleReachWorld);

void crushUnder(GameState& state, Vec2 pos, float radius) {
    ObstacleTile* found = findTile(state.obstacleCache, state.playerCount(), maps::tileOf(pos.x), maps::tileOf(pos.y));
    if (!found) return;
    ObstacleTile& tile = *found;
    int i = 0;
    while (i < tile.count) {
        const Obstacle box = tile.items[i];
        const float gapX = std::max(std::abs(pos.x - box.center.x) - box.half.x, 0.0f);
        const float gapY = std::max(std::abs(pos.y - box.center.y) - box.half.y, 0.0f);
        if (gapX * gapX + gapY * gapY >= radius * radius || state.run.crushedCount >= kMaxCrushedCars) {
            ++i;
            continue;
        }
        const maps::CarId car{box.texelX, box.texelY};
        state.run.crushed[state.run.crushedCount++] = car;
        // Taking it out closes the gap, so the next car is now at this same index.
        removeFromCache(state, car);
        // How far the nearest player was, so sound can tell how loud it is.
        const int nearest = nearestStanding(state, box.center);
        const float heard = distance(box.center, state.players[nearest < 0 ? 0 : nearest].pos);
        pushEvent(state, EventType::CarCrushed, box.center, heard, static_cast<uint8_t>(box.sprite));
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
        if (resolveCircle(state, pos, radius).hit && overlapsCar(state, pos, radius - balance::kCarFitSlack)) return before;
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

bool rescueSpot(const GameState& state, Vec2 pos, float radius, Vec2 targetPos, float touchDistance, Vec2* spot) {
    const Vec2 toTarget = targetPos - pos;
    const float gap = length(toTarget);
    if (gap < 1e-3f) return false;
    const Vec2 dir = toTarget * (1.0f / gap);
    const float reach = std::min(gap, balance::kZombieStuckLookAhead);
    // It lands just out of reach if it can, so the player gets a moment to see it.
    const float stopShort = touchDistance + balance::kZombieRescueStandOff;

    // Walk the straight line, and keep the first clear spot after the last car it crosses, so
    // one move gets past a whole pile and nothing is left between it and the target. The test
    // is a little wider than the walker, so a line that only just scrapes a car counts as
    // crossing it.
    const float wideRadius = radius + balance::kZombieRescueClearance;
    const int steps = static_cast<int>(reach / balance::kZombieRescueStep);
    bool exitFound = false;
    float exitAlong = 0.0f;
    bool inCar = false;
    for (int i = 1; i <= steps; ++i) {
        const float along = static_cast<float>(i) * balance::kZombieRescueStep;
        const bool solid = overlapsCar(state, pos + dir * along, wideRadius);
        if (inCar && !solid) {
            exitFound = true;
            exitAlong = along;
        }
        inCar = solid;
    }
    if (exitFound && !inCar && gap - exitAlong >= stopShort) {
        *spot = pos + dir * exitAlong;
        return true;
    }

    // Spots on a circle, pushed out of the cars, because free room in a pile can be too thin for
    // a spot picked blindly to land in. Each is judged by the test given.
    const auto tryCircle = [&](Vec2 center, float across, auto&& good) {
        for (int i = 0; i < balance::kZombieRescueRingSpots; ++i) {
            Vec2 at = center + fromAngle(kTau * static_cast<float>(i) / static_cast<float>(balance::kZombieRescueRingSpots)) * across;
            for (int round = 0; round < balance::kItemPushRounds; ++round) {
                if (!resolveCircle(state, at, radius).hit) break;
            }
            if (overlapsCar(state, at, radius - balance::kCarFitSlack) || !good(at)) continue;
            *spot = at;
            return true;
        }
        return false;
    };

    // Too far away to see the target: the end of the line stands in for it, and the rest of the
    // way is done by the next rescue, if it is needed at all.
    if (gap > balance::kZombieStuckLookAhead) {
        const float mostGapSq = (gap - balance::kZombieProgressMargin) * (gap - balance::kZombieProgressMargin);
        const auto closer = [&](Vec2 at) { return distanceSq(at, targetPos) < mostGapSq; };
        for (int ring = 0; ring <= balance::kZombieRescueRings; ++ring) {
            if (tryCircle(pos + dir * reach, static_cast<float>(ring) * balance::kZombieRescueStep, closer)) return true;
        }
        return false;
    }

    // The target is near, and the line ends in a car. So look around the target itself, nearest
    // first, for a spot with a clear run up to touching distance. Stopping there matters: a car
    // right beside the target may be in the way of the last few units, but not of a touch.
    const auto clearRun = [&](Vec2 at) {
        const float atGap = distance(at, targetPos);
        if (atGap >= gap - balance::kZombieProgressMargin) return false;
        if (atGap <= touchDistance) return true;
        return !lineBlocked(state, at, lerp(at, targetPos, (atGap - touchDistance) / atGap), radius);
    };
    for (int ring = 0; ring <= balance::kZombieRescueRings; ++ring) {
        if (tryCircle(targetPos, stopShort + static_cast<float>(ring) * balance::kZombieRescueStep, clearRun)) return true;
    }
    // Failing that the target is in a slot this walker can't walk into. If there is room anywhere
    // within touching distance it goes there. If not it stays put, because a move that doesn't
    // reach the target would only send it back to walk the same way again and again.
    const auto inTouch = [&](Vec2 at) { return distanceSq(at, targetPos) <= touchDistance * touchDistance; };
    for (float across = touchDistance; across > 0.0f; across -= balance::kZombieRescueStep) {
        if (tryCircle(targetPos, across, inTouch)) return true;
    }
    return false;
}

Vec2 placeItem(const GameState& state, Vec2 pos) {
    // Nearly always a single push, or none. Leaving one car can put it into the next, so a
    // pile takes more rounds than the two that walking needs.
    Vec2 spot = pos;
    for (int round = 0; round < balance::kItemPushRounds; ++round) {
        if (!resolveCircle(state, spot, balance::kItemCarRadius).hit) break;
    }
    if (!overlapsCar(state, spot, balance::kItemCarRadius - balance::kCarFitSlack) && collectableAt(state, spot)) return spot;

    // In the middle of a pile. The nearest circle around the drop with room for the player
    // is always somewhere the item can be picked up.
    for (int ring = 1; ring <= balance::kItemRings; ++ring) {
        const float across = static_cast<float>(ring) * balance::kItemRingStep;
        for (int i = 0; i < balance::kItemRingSpots; ++i) {
            const Vec2 at = pos + fromAngle(kTau * static_cast<float>(i) / static_cast<float>(balance::kItemRingSpots)) * across;
            if (playerCanWalkTo(state, at)) return at;
        }
    }
    return spot;
}

}  // namespace ls::obstacles
