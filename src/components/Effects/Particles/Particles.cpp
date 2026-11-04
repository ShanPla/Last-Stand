#include "components/Effects/Particles/Particles.h"

#include "components/Effects/Glow/Glow.h"
#include "data/Theme.h"

#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace ls::effects {

namespace {

struct Particle {
    float x = 0.0f, y = 0.0f, height = 0.0f;
    float vx = 0.0f, vy = 0.0f, lift = 0.0f;
    float gravity = 0.0f;
    float age = 0.0f;
    float life = 0.0f;  // 0 means the slot is free.
    Color color{};
    uint8_t size = 1;
    uint8_t flags = 0;
    uint8_t bounced = 0;
};

constexpr int kMaxParticles = 1400;
constexpr float kDragPerSecond = 5.0f;
constexpr float kFadeFrom = 0.55f;  // Solid until here, as a share of its life.

Particle g_particles[kMaxParticles];
int g_next = 0;
int g_live = 0;
int g_spawned = 0;
float g_amount = 1.0f;

struct Stain {
    Vec2 at;
    float age = 0.0f;
    float life = 0.0f;
    float size = 0.0f;
    uint32_t pattern = 0;
    StainKind kind = StainKind::Blood;
};

constexpr int kMaxStains = 120;
constexpr float kStainSeconds = 7.0f;
constexpr float kStainStrength = 0.62f;
Stain g_stains[kMaxStains];
int g_nextStain = 0;
int g_liveStains = 0;

// One axis-aligned square of whole art pixels, inside rlBegin(RL_QUADS).
inline void block(int x, int y, int w, int h, Color color) {
    const float l = static_cast<float>(x);
    const float t = static_cast<float>(y);
    const float r = static_cast<float>(x + w);
    const float b = static_cast<float>(y + h);
    rlColor4ub(color.r, color.g, color.b, color.a);
    rlVertex2f(l, t);
    rlVertex2f(l, b);
    rlVertex2f(r, b);
    rlVertex2f(r, t);
}

inline int toPixel(float value) { return static_cast<int>(value + 100000.5f) - 100000; }

Color tinted(Color color, const float tint[3]) {
    color.r = static_cast<unsigned char>(std::min(255.0f, static_cast<float>(color.r) * tint[0]));
    color.g = static_cast<unsigned char>(std::min(255.0f, static_cast<float>(color.g) * tint[1]));
    color.b = static_cast<unsigned char>(std::min(255.0f, static_cast<float>(color.b) * tint[2]));
    return color;
}

}  // namespace

void setParticleAmount(float amount) { g_amount = amount; }
float particleAmount() { return g_amount; }

void emitBurst(const Burst& burst, Rng& random) {
    // Rounding up keeps at least one particle, so reduced motion is still readable.
    const int count = std::min(kMaxParticles, static_cast<int>(std::ceil(static_cast<float>(burst.count) * g_amount)));
    const bool aimed = burst.aim.x != 0.0f || burst.aim.y != 0.0f;
    const float aimAngle = aimed ? std::atan2(burst.aim.y, burst.aim.x) : 0.0f;
    for (int i = 0; i < count; ++i) {
        Particle& p = g_particles[g_next];
        g_next = (g_next + 1) % kMaxParticles;
        if (p.life <= 0.0f) g_live++;
        const float angle = aimed ? aimAngle + random.range(-burst.aimSpread, burst.aimSpread) : random.range(0.0f, kTau);
        const float speed = random.range(burst.speedMin, burst.speedMax);
        const Vec2 dir = fromAngle(angle);
        p.x = burst.at.x + dir.x * burst.ringRadius;
        p.y = burst.at.y + dir.y * burst.ringRadius;
        p.height = burst.height;
        p.vx = dir.x * speed;
        p.vy = dir.y * speed;
        p.lift = random.range(burst.liftMin, burst.liftMax);
        p.gravity = burst.gravity;
        p.age = 0.0f;
        p.life = random.range(burst.lifeMin, burst.lifeMax);
        p.color = burst.colors[random.below(burst.colorCount)];
        p.size = static_cast<uint8_t>(burst.sizeMin + random.below(burst.sizeMax - burst.sizeMin + 1));
        p.flags = burst.flags;
        p.bounced = 0;
        g_spawned++;
    }
}

void updateParticles(float dt) {
    if (dt <= 0.0f) return;
    int live = 0;
    const float drag = std::max(0.0f, 1.0f - kDragPerSecond * dt);
    for (Particle& p : g_particles) {
        if (p.life <= 0.0f) continue;
        p.age += dt;
        if (p.age >= p.life) {
            p.life = 0.0f;
            continue;
        }
        live++;
        if ((p.flags & kFlagDrag) != 0) {
            p.vx *= drag;
            p.vy *= drag;
            p.lift *= drag;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.lift -= p.gravity * dt;
        p.height += p.lift * dt;
        if (p.height <= 0.0f && p.gravity > 0.0f) {
            p.height = 0.0f;
            if ((p.flags & kFlagBounce) != 0 && p.bounced == 0) {
                p.bounced = 1;
                p.lift = -p.lift * 0.35f;
                p.vx *= 0.5f;
                p.vy *= 0.5f;
            } else {
                // It has landed: it lies still for the rest of its short life.
                p.lift = p.vx = p.vy = p.gravity = 0.0f;
            }
        }
    }
    g_live = live;
}

void clearParticles() {
    for (Particle& p : g_particles) p.life = 0.0f;
    g_live = 0;
    g_next = 0;
}

void drawParticlePool(const View& view, const float tint[3], bool twinkle) {
    for (const Particle& p : g_particles) {
        if (p.life <= 0.0f) continue;
        const ScreenPoint ground = project(view, Vec2{p.x, p.y});
        if (!onTarget(view, ground, 4.0f, 120.0f, 4.0f)) continue;
        const float t = p.age / p.life;
        Color color = (p.flags & kFlagTinted) != 0 ? tinted(p.color, tint) : p.color;
        if (t > kFadeFrom) color.a = static_cast<unsigned char>(255.0f * (1.0f - (t - kFadeFrom) / (1.0f - kFadeFrom)));
        int size = p.size;
        if ((p.flags & kFlagShrink) != 0) size = std::max(1, toPixel(static_cast<float>(size) * (1.0f - t * 0.8f)));
        const int x = toPixel(ground.x) - size / 2;
        const int y = toPixel(ground.y - p.height * ground.scale) - size / 2;
        block(x, y, size, size, color);
        if (twinkle && (p.flags & kFlagTwinkle) != 0 && t < 0.5f) {
            // Two extra arms make a plus, which reads as a glint at this size.
            block(x - 1, y, size + 2, size, color);
            block(x, y - 1, size, size + 2, color);
        }
    }
}

void queueHotParticleGlows(const View& view, int most) {
    int left = most;
    for (const Particle& p : g_particles) {
        if (p.life <= 0.0f || (p.flags & kFlagHot) == 0) continue;
        if (--left < 0) return;
        const ScreenPoint ground = project(view, Vec2{p.x, p.y});
        if (!onTarget(view, ground, 8.0f, 120.0f, 8.0f)) continue;
        queueGlow(ground.x, ground.y - p.height * ground.scale, 3.0f + static_cast<float>(p.size) * 2.0f, p.color, 0.45f * (1.0f - p.age / p.life));
    }
}

int liveParticles() { return g_live; }
int spawnedParticles() { return g_spawned; }

// --- Ground stains ---------------------------------------------------------------

void addStain(Vec2 at, float size, StainKind kind, Rng& random) {
    Stain& stain = g_stains[g_nextStain];
    g_nextStain = (g_nextStain + 1) % kMaxStains;
    if (stain.life <= 0.0f) g_liveStains++;
    stain.at = at;
    stain.age = 0.0f;
    stain.life = kStainSeconds * random.range(0.8f, 1.2f);
    stain.size = size;
    stain.pattern = random.nextU32();
    stain.kind = kind;
}

void updateStains(float dt) {
    if (dt <= 0.0f) return;
    int live = 0;
    for (Stain& stain : g_stains) {
        if (stain.life <= 0.0f) continue;
        stain.age += dt;
        if (stain.age >= stain.life) {
            stain.life = 0.0f;
            continue;
        }
        live++;
    }
    g_liveStains = live;
}

void clearStains() {
    for (Stain& stain : g_stains) stain.life = 0.0f;
    g_liveStains = 0;
    g_nextStain = 0;
}

void drawStainPool(const View& view, const float tint[3]) {
    for (const Stain& stain : g_stains) {
        if (stain.life <= 0.0f) continue;
        const ScreenPoint ground = project(view, stain.at);
        const float size = stain.size * ground.scale;
        if (!onTarget(view, ground, size * 3.0f, size * 3.0f, size * 3.0f)) continue;
        const Color base = stain.kind == StainKind::Scorch ? theme::kFxStainScorch : (stain.kind == StainKind::Bile ? theme::kFxStainBile : theme::kFxStainBlood);
        Color color = tinted(base, tint);
        const float t = stain.age / stain.life;
        color.a = static_cast<unsigned char>(255.0f * kStainStrength * (t < 0.6f ? 1.0f : 1.0f - (t - 0.6f) / 0.4f));
        // A flat blob with a few drops around it, all on whole pixels. The ground is
        // squashed, so a blob is drawn wider than it is tall, like a real puddle.
        const int width = std::max(2, toPixel(size * 2.0f));
        const int height = std::max(1, toPixel(size * 2.0f * worldview::kTiltSquash));
        const int x = toPixel(ground.x);
        const int y = toPixel(ground.y);
        block(x - width / 2, y - height / 2, width, height, color);
        block(x - width / 2 + 1, y - height / 2 - 1, std::max(1, width - 2), height + 2, color);
        uint32_t bits = stain.pattern;
        constexpr int kDrops = 4;
        for (int i = 0; i < kDrops; ++i) {
            const int spreadX = static_cast<int>(bits & 15u) - 8;
            const int spreadY = static_cast<int>((bits >> 4) & 7u) - 4;
            const int dotSize = 1 + static_cast<int>((bits >> 7) & 1u);
            block(x + spreadX * width / 7, y + spreadY * height / 3, dotSize, dotSize, color);
            bits = (bits >> 8) | (bits << 24);
        }
    }
}

int liveStains() { return g_liveStains; }

}  // namespace ls::effects
