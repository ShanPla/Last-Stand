#include "components/Effects/Bursts/Bursts.h"

#include "data/Theme.h"

namespace ls::effects {

namespace {

Rng g_randomBursts;  // Looks only, so it is never the simulation's generator.

}  // namespace

void bloodSpray(Vec2 at, float height, Vec2 aim, int count, const Color* colors) {
    Burst drops;
    drops.at = at;
    drops.height = height;
    drops.count = count;
    drops.speedMin = 40.0f;
    drops.speedMax = 120.0f;
    drops.liftMin = 10.0f;
    drops.liftMax = 60.0f;
    drops.lifeMin = 0.28f;
    drops.lifeMax = 0.5f;
    drops.gravity = 300.0f;
    drops.sizeMin = 1;
    drops.sizeMax = 2;
    drops.colors = colors;
    drops.colorCount = 3;
    drops.aim = aim;
    drops.aimSpread = 0.9f;
    drops.flags = kFlagTinted;
    emitBurst(drops, g_randomBursts);
}

void impactSparks(Vec2 at, float height, Vec2 back, int count, const Color* colors) {
    Burst sparks;
    sparks.at = at;
    sparks.height = height;
    sparks.count = count;
    sparks.speedMin = 90.0f;
    sparks.speedMax = 230.0f;
    sparks.liftMin = -20.0f;
    sparks.liftMax = 50.0f;
    sparks.lifeMin = 0.07f;
    sparks.lifeMax = 0.16f;
    sparks.gravity = 120.0f;
    sparks.sizeMin = 1;
    sparks.sizeMax = 1;
    sparks.colors = colors;
    sparks.colorCount = 3;
    sparks.aim = back;
    sparks.aimSpread = 1.1f;
    sparks.flags = kFlagHot;
    emitBurst(sparks, g_randomBursts);
}

void emberRise(Vec2 at, float height, int count, float spread) {
    Burst embers;
    embers.at = at;
    embers.height = height;
    embers.count = count;
    embers.speedMax = spread;
    embers.liftMin = 18.0f;
    embers.liftMax = 60.0f;
    embers.lifeMin = 0.25f;
    embers.lifeMax = 0.6f;
    embers.gravity = 0.0f;
    embers.sizeMin = 1;
    embers.sizeMax = 1;
    embers.colors = theme::kFxEmber;
    embers.colorCount = 3;
    embers.flags = kFlagHot | kFlagDrag;
    emitBurst(embers, g_randomBursts);
}

void chunkBurst(Vec2 at, float height, int count, const Color* colors, int colorCount, float speed, int sizeMax) {
    Burst chunks;
    chunks.at = at;
    chunks.height = height;
    chunks.count = count;
    chunks.speedMin = speed * 0.3f;
    chunks.speedMax = speed;
    chunks.liftMin = 40.0f;
    chunks.liftMax = 120.0f;
    chunks.lifeMin = 0.45f;
    chunks.lifeMax = 0.85f;
    chunks.gravity = 340.0f;
    chunks.sizeMin = 2;
    chunks.sizeMax = sizeMax;
    chunks.colors = colors;
    chunks.colorCount = colorCount;
    chunks.flags = kFlagBounce | kFlagTinted;
    emitBurst(chunks, g_randomBursts);
}

void groundRing(Vec2 at, int count, float speed, const Color* colors, int colorCount, float life, int size, uint8_t flags) {
    Burst ring;
    ring.at = at;
    ring.height = 2.0f;
    ring.count = count;
    ring.speedMin = speed * 0.75f;
    ring.speedMax = speed;
    ring.liftMin = 4.0f;
    ring.liftMax = 26.0f;
    ring.lifeMin = life * 0.7f;
    ring.lifeMax = life;
    ring.gravity = 30.0f;
    ring.sizeMin = 1;
    ring.sizeMax = size;
    ring.colors = colors;
    ring.colorCount = colorCount;
    ring.flags = flags | kFlagShrink;
    emitBurst(ring, g_randomBursts);
}

void fountain(Vec2 at, int count, const Color* colors, float reach, float life) {
    Burst sparks;
    sparks.at = at;
    sparks.height = 8.0f;
    sparks.count = count;
    sparks.speedMin = reach * 0.25f;
    sparks.speedMax = reach;
    sparks.liftMin = 60.0f;
    sparks.liftMax = 170.0f;
    sparks.lifeMin = life * 0.6f;
    sparks.lifeMax = life;
    sparks.gravity = 200.0f;
    sparks.sizeMin = 1;
    sparks.sizeMax = 2;
    sparks.colors = colors;
    sparks.colorCount = 3;
    sparks.flags = kFlagHot | kFlagTwinkle | kFlagShrink;
    emitBurst(sparks, g_randomBursts);
}

void sparkle(Vec2 at, int count, const Color* colors, int colorCount) {
    Burst glints;
    glints.at = at;
    glints.height = 6.0f;
    glints.count = count;
    glints.speedMax = 30.0f;
    glints.liftMin = 10.0f;
    glints.liftMax = 44.0f;
    glints.lifeMin = 0.25f;
    glints.lifeMax = 0.45f;
    glints.gravity = 0.0f;
    glints.sizeMin = 1;
    glints.sizeMax = 1;
    glints.colors = colors;
    glints.colorCount = colorCount;
    glints.flags = kFlagTwinkle;
    emitBurst(glints, g_randomBursts);
}

void hotBurst(Vec2 at, float height, int count, float speed, float life, const Color* colors) {
    Burst burst;
    burst.at = at;
    burst.height = height;
    burst.count = count;
    burst.speedMin = 60.0f;
    burst.speedMax = speed;
    burst.liftMin = -60.0f;
    burst.liftMax = 200.0f;
    burst.lifeMin = life * 0.4f;
    burst.lifeMax = life;
    burst.gravity = 240.0f;
    burst.sizeMin = 1;
    burst.sizeMax = 3;
    burst.colors = colors;
    burst.colorCount = 3;
    burst.flags = kFlagHot | kFlagShrink;
    emitBurst(burst, g_randomBursts);
}

}  // namespace ls::effects
