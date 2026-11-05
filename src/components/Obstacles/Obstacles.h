#pragma once

#include "core/GameState.h"

// Solid cars, as the simulation sees them: axis-aligned boxes that come from the map's
// layout. There is no pathfinding. The player and the zombies slide along a car's sides, and
// a zombie that is about to walk into one turns toward its nearer corner.
namespace ls::obstacles {

// Brings the cached lists up to date around the player. Runs first in every step, and costs
// nothing until the player crosses into a new tile.
void update(GameState& state);

// Throws the cache away, for a new run or a different map.
void invalidate(GameState& state);

struct Push {
    bool hit = false;
    Vec2 normal;  // Away from the car, for what was hit last.
};

// Moves a circle out of any car it overlaps, by the shortest way. Corners are rounded, so a
// circle sliding along a car doesn't catch on the corner of the next one.
Push resolveCircle(const GameState& state, Vec2& pos, float radius);

// Moves a circle from one place to another in steps small enough that it can't jump over a
// car, sliding along whatever it meets. Returns where it ended up.
Vec2 sweepCircle(const GameState& state, Vec2 from, Vec2 to, float radius);

// A wanted velocity that goes around a car instead of into it. It is the same velocity
// when no car is close ahead. targetPos is what the walker is trying to reach, and side
// breaks a tie between the two ways around.
Vec2 steerAround(const GameState& state, Vec2 pos, float radius, Vec2 velocity, Vec2 targetPos, uint32_t side);

// True when a circle at this point overlaps a car.
bool overlapsCar(const GameState& state, Vec2 pos, float radius);

// True when a straight line wide enough for this radius would run into a car. The line must
// be shorter than a tile.
bool lineBlocked(const GameState& state, Vec2 from, Vec2 to, float radius);

// How far from the nearest car's edge a point is, up to limit. Used by the balance tool.
float distanceToNearestCar(const GameState& state, Vec2 pos, float limit);

// The nearest place a stuck walker can stand: past the corner of the car that is holding
// it, on the side nearer the target. Returns false when no car is close.
bool cornerToward(const GameState& state, Vec2 pos, float radius, Vec2 targetPos, Vec2* corner);

}  // namespace ls::obstacles
