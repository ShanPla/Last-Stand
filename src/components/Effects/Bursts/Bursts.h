#pragma once

#include "components/Effects/Particles/Particles.h"

// Small named recipes for particles, so the code that reacts to an event reads like a
// list of what happens, not a wall of numbers. Each picks its own speeds and lifetimes.
namespace ls::effects {

// Blood or bile thrown away from a hit. colors holds three tones.
void bloodSpray(Vec2 at, float height, Vec2 aim, int count, const Color* colors);
// Short, bright sparks, for bullet impacts and muzzles. colors holds three tones.
void impactSparks(Vec2 at, float height, Vec2 back, int count, const Color* colors);
// Slow embers drifting upward, after flames and blasts.
void emberRise(Vec2 at, float height, int count, float spread);
// Heavy pieces that fly up, fall and bounce: gore and rubble.
void chunkBurst(Vec2 at, float height, int count, const Color* colors, int colorCount, float speed, int sizeMax);
// A ring of fast particles running outward along the ground, for the biggest moments.
void groundRing(Vec2 at, int count, float speed, const Color* colors, int colorCount, float life, int size, uint8_t flags);
// Sparks that fountain up and fall around him: level-ups and evolutions.
void fountain(Vec2 at, int count, const Color* colors, float reach, float life);
// A few small glints that rise and fade, for gems and pickups.
void sparkle(Vec2 at, int count, const Color* colors, int colorCount);
// A big glowing burst in every direction, for the boss. colors holds three tones.
void hotBurst(Vec2 at, float height, int count, float speed, float life, const Color* colors);

}  // namespace ls::effects
