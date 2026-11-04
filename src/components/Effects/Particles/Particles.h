#pragma once

#include "components/WorldView/Projection.h"
#include "core/Math.h"
#include "core/Rng.h"

#include "raylib.h"

#include <cstdint>

// Small squares of whole art pixels, with a fixed pool allocated once. A burst just
// overwrites the oldest particles when the pool is full, so a huge fight can never use
// more memory or more time than a small one.
namespace ls::effects {

using worldview::View;
using worldview::ScreenPoint;
using worldview::project;
using worldview::onTarget;

enum ParticleFlags : uint8_t {
    kFlagNone = 0,
    kFlagShrink = 1,    // Gets smaller as it ages.
    kFlagBounce = 2,    // Bounces once off the ground rather than stopping.
    kFlagDrag = 4,      // Slows quickly, for sparks and dust.
    kFlagTwinkle = 8,   // Drawn as a small plus, then a dot.
    kFlagHot = 16,      // Gives off light, so it adds a tiny glow.
    kFlagTinted = 32,   // Takes the time of day, for things that aren't light sources.
};

// One kind of burst. Speeds are in world units per second across the ground, and art
// pixels per second upward. A color is picked from the list for each particle.
struct Burst {
    Vec2 at;
    float height = 14.0f;       // Art pixels above the ground.
    int count = 1;
    float speedMin = 0.0f;
    float speedMax = 60.0f;
    float liftMin = 0.0f;
    float liftMax = 40.0f;
    float lifeMin = 0.3f;
    float lifeMax = 0.5f;
    float gravity = 240.0f;     // Art pixels per second squared.
    int sizeMin = 1;
    int sizeMax = 2;
    const Color* colors = nullptr;
    int colorCount = 1;
    Vec2 aim{0.0f, 0.0f};       // Favors this direction when set.
    float aimSpread = 1.0f;     // Radians either side of aim.
    float ringRadius = 0.0f;    // Starts on a circle this wide, not at one point.
    uint8_t flags = kFlagNone;
};

// How many particles are wanted: 1 normally, less for weak devices and reduced motion.
void setParticleAmount(float amount);
float particleAmount();

void emitBurst(const Burst& burst, Rng& random);
void updateParticles(float dt);
void clearParticles();

// Flat quads, between rlBegin and rlEnd. tint is the time of day for tinted particles.
void drawParticlePool(const View& view, const float tint[3], bool twinkle);

// Queues a small glow for each hot particle. Call before the glow layer is drawn.
void queueHotParticleGlows(const View& view, int most);

int liveParticles();
int spawnedParticles();

// --- Ground stains ---
enum class StainKind : uint8_t { Blood, Bile, Scorch };

void addStain(Vec2 at, float size, StainKind kind, Rng& random);
void updateStains(float dt);
void clearStains();
void drawStainPool(const View& view, const float tint[3]);
int liveStains();

}  // namespace ls::effects
