#include "components/WorldView/Objects.h"

#include "components/Effects/Effects.h"
#include "components/Weapons/Blades/Blades.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

constexpr int kManyShots = 1200;  // Above this, bullets drop to a single quad.
constexpr int kMaxShotGlows = 320;  // The most shots lit in one frame.
constexpr float kShotHeight = 14.0f;  // Art pixels above the ground: about chest height.
constexpr float kFlameHeight = 10.0f;
constexpr float kGrenadeArcHeight = 46.0f;
constexpr float kBladeHeight = 11.0f;
constexpr float kPickupHeight = 2.0f;
constexpr float kPickupBob = 3.0f;
constexpr int kTeslaSegments = 7;
constexpr float kTeslaJitter = 9.0f;  // Art pixels.
constexpr int kTeslaRefreshTicks = 2;
constexpr float kBossLineChevronSpacing = 120.0f;

// A long boss line can reach behind the camera, where perspective breaks down. Only
// the part of it that can be seen is drawn.
constexpr float kLineNearLimit = 1900.0f;
constexpr float kLineFarLimit = -2400.0f;

float flameLifetime() { return balance::kWeaponLevels[static_cast<int>(WeaponId::Flamethrower)][0].duration; }
float grenadeFuse() { return balance::kWeaponLevels[static_cast<int>(WeaponId::GrenadeLauncher)][0].duration; }

Vec2 toScreen(const ScreenPoint& point) { return {point.x, point.y}; }
Vec2 toScreen(const View& view, Vec2 world) { return toScreen(project(view, world)); }

// Which way a world direction points in the picture.
Vec2 screenDirection(Vec2 worldDir) {
    return normalizedOr(Vec2{worldDir.x, worldDir.y * kTiltSquash}, Vec2{1.0f, 0.0f});
}

// --- Player shots ------------------------------------------------------------

void drawBullet(const Projectile& shot, const ScreenPoint& ground, bool cheap) {
    const bool pellet = shot.radius < balance::kBulletRadius - 0.5f;
    const Vec2 dir = screenDirection(shot.vel);
    const float length = (pellet ? 3.0f : 5.0f) * ground.scale;
    const float headWidth = pellet ? 1.6f : 2.0f;
    const Color color = shot.evolved ? theme::kEvolvedAccent : (pellet ? theme::kPellet : theme::kPlayerShot);
    const Vec2 at{ground.x, ground.y - kShotHeight * ground.scale};
    const Vec2 head = at + dir * length * 0.5f;
    const Vec2 tail = at - dir * length * 1.6f;
    const Vec2 side = perpendicular(dir) * (headWidth * 0.5f);
    // A tracer: bright head, fading tail. The low-resolution target gives it hard pixels.
    gradientQuad(head + side, head - side, color, tail, tail, fade(color, 0.0f));
    if (!cheap) {
        const Vec2 coreTail = at - dir * length * 0.2f;
        const Vec2 coreSide = perpendicular(dir) * 0.5f;
        quad(head + coreSide, head - coreSide, coreTail - coreSide, coreTail + coreSide, theme::kShotCore);
    }
}

}  // namespace

void drawShots(const View& view, const GameState& state, float alpha) {
    const bool cheap = state.projectiles.count > kManyShots;
    for (const Projectile& shot : state.projectiles) {
        if (shot.kind != ProjectileKind::Bullet) continue;
        const ScreenPoint ground = project(view, lerp(shot.prevPos, shot.pos, alpha));
        if (!onTarget(view, ground, 16.0f, 30.0f, 16.0f)) continue;
        drawBullet(shot, ground, cheap);
    }
}

// --- Zones -------------------------------------------------------------------

namespace {

// The nearest of the baked puffs, for a wanted radius in art pixels.
const SpriteRef& puffSprite(float radius, uint32_t variant) {
    int size = 0;
    if (radius > 10.0f) size = 3;
    else if (radius > 6.5f) size = 2;
    else if (radius > 4.0f) size = 1;
    return itemSprite(static_cast<Item>(static_cast<int>(Item::Puff0) + size * 2 + static_cast<int>(variant & 1u)));
}

// Chunky fire: puffs thrown outward from the center, colored by how hot they still
// are. Their positions come from the blast's place, so each blast looks different.
void drawExplosionFire(const View& view, const Zone& zone) {
    const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
    const float age = 1.0f - life;
    const float radius = std::max(6.0f, zone.radius);
    const uint32_t seed = hashPosition(zone.a);
    const ScreenPoint center = project(view, zone.a);
    const float artRadius = radius * kArtPerWorld * center.scale;
    constexpr int kPuffs = 20;
    // Solid until the very end: see-through puffs would look like soap bubbles. Three
    // passes put the coolest fire at the back and the hottest in front of it.
    const auto strength = static_cast<uint8_t>(255.0f * std::min(1.0f, life * 4.0f));
    for (int pass = 0; pass < 3; ++pass) {
        for (int i = 0; i < kPuffs; ++i) {
            const uint32_t h = scramble(seed + static_cast<uint32_t>(i) * 977u);
            const float spread = 0.25f + 0.75f * static_cast<float>((h >> 8) & 255u) / 255.0f;
            const float heat = life + (1.0f - spread) * 0.5f - 0.1f;
            const int band = heat > 0.65f ? 2 : (heat > 0.3f ? 1 : 0);
            if (band != pass) continue;
            const float angle = static_cast<float>(h & 255u) / 255.0f * kTau;
            const float reach = artRadius * spread * (0.5f + 0.5f * age);
            // Fire rises: it spreads flat at first, then lifts as it burns out.
            const float x = center.x + std::cos(angle) * reach;
            const float y = center.y + std::sin(angle) * reach * kTiltSquash - artRadius * 0.45f * age - 3.0f;
            const float size = artRadius * (0.26f + 0.22f * static_cast<float>((h >> 16) & 7u) / 7.0f) * (1.2f - 0.6f * age);
            uint8_t row = band == 2 ? kRowFireHot : (band == 1 ? kRowBase : kRowFireOld);
            if (zone.evolved) row = kRowEvolved;
            spriteQuad(puffSprite(size, h >> 20), roundToInt(x), roundToInt(y), false, row, 0, strength);
        }
    }
}

void drawFlame(const Projectile& shot, const ScreenPoint& ground, uint32_t tick) {
    const float life = clampf(shot.life / flameLifetime(), 0.0f, 1.0f);
    const uint32_t seed = hashPosition(shot.prevPos) >> 4;
    const float flick = effects::flickerAllowed() ? 0.85f + 0.3f * triangleWave(tick + seed, 8u) : 1.0f;
    // A flame starts small and hot at the nozzle, swells, then thins out and rises.
    const float size = shot.radius * kArtPerWorld * ground.scale * (1.45f - 0.8f * life) * flick;
    uint8_t row = life > 0.62f ? kRowFireHot : (life > 0.28f ? kRowBase : kRowFireOld);
    if (shot.evolved) row = kRowEvolved;
    const float x = ground.x + static_cast<float>(seed % 5u) - 2.0f;
    const float y = ground.y - kFlameHeight * ground.scale - (1.0f - life) * 8.0f;
    const auto strength = static_cast<uint8_t>(255.0f * std::min(1.0f, 0.25f + life * 1.8f));
    spriteQuad(puffSprite(size, seed), roundToInt(x), roundToInt(y), false, row, 0, strength);
}

void drawBossCircle(const View& view, const Zone& zone, uint32_t tick) {
    const float radius = zone.maxRadius > 0.0f ? zone.maxRadius : zone.radius;
    const float progress = zone.maxLife > 0.0f ? clampf(1.0f - zone.life / zone.maxLife, 0.0f, 1.0f) : 1.0f;
    // Pulses faster right before it lands, so the moment to leave is hard to miss. Where
    // flicker is off it stays steady, and the closing ring still shows the timing.
    const float pulse = effects::flickerAllowed() ? triangleWave(tick, progress > 0.75f ? 8u : 24u) : 0.5f;

    groundDisc(view, zone.a, radius, 2, fade(theme::kHostileFill, 0.18f));
    groundDisc(view, zone.a, std::max(1.0f, radius * progress), 2, fade(theme::kHostileFill, 0.34f + 0.20f * pulse));
    groundRing(view, zone.a, radius * progress, 6.0f, 2, theme::kHostileFront);
    // A dark band under the rim, so it stands out even on a bright lit patch of road.
    groundRing(view, zone.a, radius + 4.0f, 14.0f, 2, fade(theme::kHostileDark, 0.7f));
    groundRing(view, zone.a, radius, 8.0f, 2, theme::kHostileEdge);

    // Sharp teeth around the rim keep it a threat, even with no color at all.
    constexpr int kToothStep = 3;
    for (int i = 0; i < kCirclePoints; i += kToothStep) {
        const Vec2 unit = g_circle.points[i];
        const Vec2 next = g_circle.points[(i + kToothStep) % kCirclePoints];
        const Vec2 tipUnit = g_circle.points[(i + kToothStep / 2) % kCirclePoints];
        const float depth = radius * (0.82f - 0.05f * pulse);
        const Vec2 a = toScreen(view, zone.a + unit * radius);
        const Vec2 b = toScreen(view, zone.a + next * radius);
        const Vec2 tip = toScreen(view, zone.a + tipUnit * depth);
        quad(a, b, tip, tip, theme::kHostileEdge);
    }
}

void drawBossLine(const View& view, const Zone& zone, uint32_t tick) {
    const float progress = zone.maxLife > 0.0f ? clampf(1.0f - zone.life / zone.maxLife, 0.0f, 1.0f) : 1.0f;
    const Vec2 worldDir = normalizedOr(zone.b - zone.a, Vec2{1.0f, 0.0f});
    const float worldLength = distance(zone.a, zone.b);

    // Trim the line to where the camera can draw it.
    float from = 0.0f;
    float to = worldLength;
    if (std::abs(worldDir.y) > 1e-3f) {
        const float limitA = (view.focusY + kLineNearLimit - zone.a.y) / worldDir.y;
        const float limitB = (view.focusY + kLineFarLimit - zone.a.y) / worldDir.y;
        from = std::max(from, std::min(limitA, limitB));
        to = std::min(to, std::max(limitA, limitB));
    }
    if (from >= to) return;

    const Vec2 side = perpendicular(worldDir) * zone.radius;
    const Vec2 startWorld = zone.a + worldDir * from;
    const Vec2 endWorld = zone.a + worldDir * to;
    const float frontDistance = worldLength * progress;
    const Vec2 frontWorld = zone.a + worldDir * clampf(frontDistance, from, to);
    const float pulse = effects::flickerAllowed() ? triangleWave(tick, progress > 0.75f ? 8u : 24u) : 0.5f;

    const Vec2 a0 = toScreen(view, startWorld + side);
    const Vec2 a1 = toScreen(view, startWorld - side);
    const Vec2 b0 = toScreen(view, endWorld - side);
    const Vec2 b1 = toScreen(view, endWorld + side);
    const Vec2 f0 = toScreen(view, frontWorld - side);
    const Vec2 f1 = toScreen(view, frontWorld + side);

    quad(a0, a1, b0, b1, fade(theme::kHostileFill, 0.18f));
    quad(a0, a1, f0, f1, fade(theme::kHostileFill, 0.34f + 0.20f * pulse));
    thickLine(a0, b1, 6.0f, fade(theme::kHostileDark, 0.7f));
    thickLine(a1, b0, 6.0f, fade(theme::kHostileDark, 0.7f));
    thickLine(a0, b1, 4.0f, theme::kHostileEdge);
    thickLine(a1, b0, 4.0f, theme::kHostileEdge);
    thickLine(f0, f1, 5.0f, theme::kHostileFront);

    // Sharp arrows along the strip, like the circle's teeth.
    const int first = static_cast<int>(from / kBossLineChevronSpacing) + 1;
    const int last = std::min(first + 32, static_cast<int>(to / kBossLineChevronSpacing));
    for (int i = first; i <= last; ++i) {
        const float along = kBossLineChevronSpacing * static_cast<float>(i);
        const Vec2 tip = toScreen(view, zone.a + worldDir * (along + zone.radius * 0.6f));
        const Vec2 wingA = toScreen(view, zone.a + worldDir * along + side * 0.7f);
        const Vec2 wingB = toScreen(view, zone.a + worldDir * along - side * 0.7f);
        const Color color = along < frontDistance ? theme::kHostileFront : fade(theme::kHostileEdge, 0.6f);
        thickLine(wingA, tip, 3.0f, color);
        thickLine(wingB, tip, 3.0f, color);
    }
}

void drawSniperBeam(const View& view, const Zone& zone) {
    const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
    const ScreenPoint pa = project(view, zone.a);
    const ScreenPoint pb = project(view, zone.b);
    const Vec2 a{pa.x, pa.y - kShotHeight * pa.scale};
    const Vec2 b{pb.x, pb.y - kShotHeight * pb.scale};
    const Color color = zone.evolved ? theme::kEvolvedAccent : theme::kBeam;
    thickLine(a, b, 1.0f + 3.0f * life, fade(color, 0.7f * life));
    thickLine(a, b, std::max(1.0f, 1.6f * life), fade(theme::kBeamCore, life));
}

// A jagged line, repeatable because it comes only from tick and position.
void drawTeslaLink(const View& view, const Zone& zone, uint32_t tick) {
    const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
    const ScreenPoint pa = project(view, zone.a);
    const ScreenPoint pb = project(view, zone.b);
    const Vec2 a{pa.x, pa.y - kShotHeight * pa.scale};
    const Vec2 b{pb.x, pb.y - kShotHeight * pb.scale};
    const Vec2 normal = perpendicular(normalizedOr(b - a, Vec2{1.0f, 0.0f}));
    // The arc reshapes every few ticks. Where flicker is off, each arc keeps one shape.
    const uint32_t reshape = effects::flickerAllowed() ? tick / kTeslaRefreshTicks : 0u;
    const uint32_t seed = hashPosition(zone.a) ^ reshape * 2654435761u;
    const Color color = zone.evolved ? theme::kEvolvedAccent : theme::kTesla;

    Vec2 points[kTeslaSegments + 1];
    for (int i = 0; i <= kTeslaSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kTeslaSegments);
        const bool end = i == 0 || i == kTeslaSegments;
        const uint32_t random = scramble(seed + static_cast<uint32_t>(i));
        const float offset = end ? 0.0f : (static_cast<float>(random & 255u) / 127.5f - 1.0f) * kTeslaJitter;
        points[i] = lerp(a, b, t) + normal * offset;
    }
    for (int i = 0; i < kTeslaSegments; ++i) thickLine(points[i], points[i + 1], 3.0f, fade(color, 0.8f * life));
    for (int i = 0; i < kTeslaSegments; ++i) thickLine(points[i], points[i + 1], 1.2f, fade(theme::kTeslaCore, life));
    pixelBlock(b.x, b.y, 2.0f + 3.0f * life, fade(theme::kTeslaCore, life));
}

const WeaponSlot* heldBlades(const GameState& state) {
    for (int i = 0; i < state.player.weaponCount; ++i) {
        if (state.player.weapons[i].id == WeaponId::Blades) return &state.player.weapons[i];
    }
    return nullptr;
}

}  // namespace

void drawGroundMarks(const View& view, const GameState& state, float alpha) {
    const uint32_t tick = state.run.tick;
    for (const Zone& zone : state.zones) {
        if (zone.kind == ZoneKind::BossCircle) drawBossCircle(view, zone, tick);
        else if (zone.kind == ZoneKind::BossLine) drawBossLine(view, zone, tick);
    }
    for (const Projectile& shot : state.projectiles) {
        if (shot.kind != ProjectileKind::Mine) continue;
        const Vec2 world = lerp(shot.prevPos, shot.pos, alpha);
        const ScreenPoint point = project(view, world);
        if (!onTarget(view, point, 30.0f, 30.0f, 30.0f)) continue;
        // Faint ring: the distance at which a zombie sets it off.
        const Color color = shot.evolved ? theme::kEvolvedAccent : theme::kPlayer;
        groundRing(view, world, balance::kMineTriggerRadius, 2.0f, 3, fade(color, 0.28f));
    }
}

void drawBlasts(const View& view, const GameState& state) {
    for (const Zone& zone : state.zones) {
        if (zone.kind != ZoneKind::Explosion) continue;
        const ScreenPoint point = project(view, zone.a);
        const float reach = zone.radius * kArtPerWorld * point.scale + 16.0f;
        if (!onTarget(view, point, reach, reach, reach)) continue;
        // A burning ring on the ground. The fire itself is drawn with the sprites.
        const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
        const Color ring = zone.evolved ? theme::kBlastEvolvedOuter : theme::kBlastOrange;
        groundRing(view, zone.a, std::max(6.0f, zone.radius), 5.0f + 8.0f * life, 3, fade(ring, 0.75f * life));
    }
}

void drawBeams(const View& view, const GameState& state) {
    for (const Zone& zone : state.zones) {
        if (zone.kind == ZoneKind::SniperBeam) drawSniperBeam(view, zone);
        else if (zone.kind == ZoneKind::TeslaLink) drawTeslaLink(view, zone, state.run.tick);
    }
}

void drawGlows(const View& view, const GameState& state) {
    for (const Zone& zone : state.zones) {
        if (zone.kind != ZoneKind::Explosion) continue;
        const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
        const float light = effects::explosionLight(life);
        const Color glow = zone.evolved ? theme::kGlowEvolved : theme::kGlowWarm;
        groundDisc(view, zone.a, zone.radius * 1.5f, 4, fade(glow, light));
        groundDisc(view, zone.a, zone.radius * 0.9f, 4, fade(glow, light));
    }
}

void queueShotGlows(const View& view, const GameState& state, float alpha) {
    if (!effects::glowEnabled()) return;
    // Blasts first, as they matter most. Each has a fast flash at the start.
    for (const Zone& zone : state.zones) {
        const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
        if (zone.kind == ZoneKind::Explosion) {
            const ScreenPoint point = project(view, zone.a);
            const float reach = zone.radius * kArtPerWorld * point.scale;
            const float y = point.y - 6.0f * point.scale;
            const float light = effects::explosionLight(life);
            effects::queueGlowSpot(point.x, y, reach * 1.35f, zone.evolved ? theme::kFxGlowEvolved : theme::kFxGlowBlast, 0.5f * light);
            effects::queueGlowSpot(point.x, y, reach * 0.6f, theme::kFxGlowBlastCore, 0.7f * light * life * life);
        } else if (zone.kind == ZoneKind::SniperBeam || zone.kind == ZoneKind::TeslaLink) {
            // A beam is lit from a few spots along it, which is plenty at this size.
            const ScreenPoint a = project(view, zone.a);
            const ScreenPoint b = project(view, zone.b);
            const Color color = zone.evolved ? theme::kFxGlowEvolved : (zone.kind == ZoneKind::TeslaLink ? theme::kFxGlowTesla : theme::kFxGlowBeam);
            constexpr int kSpots = 4;
            for (int i = 0; i <= kSpots; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kSpots);
                effects::queueGlowSpot(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t - kShotHeight * a.scale, 14.0f, color, 0.5f * life);
            }
        }
    }
    // With thousands of shots on screen, every Nth one is lit. Nobody can count them, and
    // this keeps the layer's cost fixed however many there are.
    const int stride = 1 + state.projectiles.count / kMaxShotGlows;
    for (int i = 0; i < state.projectiles.count; i += stride) {
        const Projectile& shot = state.projectiles[i];
        if (shot.kind == ProjectileKind::Mine || shot.kind == ProjectileKind::Grenade) continue;
        const ScreenPoint ground = project(view, lerp(shot.prevPos, shot.pos, alpha));
        if (!onTarget(view, ground, 16.0f, 30.0f, 16.0f)) continue;
        if (shot.kind == ProjectileKind::Flame) {
            effects::queueGlowSpot(ground.x, ground.y - kFlameHeight * ground.scale, 15.0f * ground.scale, shot.evolved ? theme::kFxGlowEvolved : theme::kFxGlowFlame, 0.34f);
        } else if (shot.kind == ProjectileKind::Rocket) {
            effects::queueGlowSpot(ground.x, ground.y - kShotHeight * ground.scale, 18.0f, shot.evolved ? theme::kFxGlowEvolved : theme::kFxGlowFlame, 0.6f);
        } else {
            const bool pellet = shot.radius < balance::kBulletRadius - 0.5f;
            effects::queueGlowSpot(ground.x, ground.y - kShotHeight * ground.scale, pellet ? 7.0f : 10.0f, shot.evolved ? theme::kFxGlowEvolved : theme::kFxGlowShot, 0.55f);
        }
    }
}

void drawBladeTrails(const View& view, const GameState& state, float alpha) {
    const WeaponSlot* blades = heldBlades(state);
    if (!blades) return;
    const balance::WeaponLevel stats = weapons::effectiveStats(state, *blades);
    const int count = std::max(1, stats.count);
    const float angle = state.run.bladeAngle - stats.speed * kDt * (1.0f - alpha);
    const Vec2 center = lerp(state.player.prevPos, state.player.pos, alpha);
    const Color color = blades->evolved ? theme::kEvolvedAccent : theme::kBladeTrail;
    if (blades->evolved) groundRing(view, center, stats.area, 3.0f, 2, fade(color, 0.4f));
    constexpr int kTrailSegments = 5;
    constexpr float kTrailArc = 0.14f;
    for (int i = 0; i < count; ++i) {
        const float bladeAngle = angle + kTau * static_cast<float>(i) / static_cast<float>(count);
        const float radius = weapons::bladeOrbitRadius(i, stats);
        for (int s = 0; s < kTrailSegments; ++s) {
            const float a0 = bladeAngle - kTrailArc * static_cast<float>(s);
            const float a1 = bladeAngle - kTrailArc * static_cast<float>(s + 1);
            const ScreenPoint p0 = project(view, center + fromAngle(a0) * radius);
            const ScreenPoint p1 = project(view, center + fromAngle(a1) * radius);
            const float lift = kBladeHeight * p0.scale;
            const float strength = 0.55f * (1.0f - static_cast<float>(s) / static_cast<float>(kTrailSegments));
            thickLine(Vec2{p0.x, p0.y - lift}, Vec2{p1.x, p1.y - lift}, 2.0f, fade(color, strength));
        }
    }
}

// --- Sprites below the figures -------------------------------------------------

void drawGroundSprites(const View& view, const GameState& state, float alpha) {
    const uint32_t tick = state.run.tick;
    for (const Projectile& shot : state.projectiles) {
        if (shot.kind != ProjectileKind::Mine) continue;
        const ScreenPoint point = project(view, lerp(shot.prevPos, shot.pos, alpha));
        if (!onTarget(view, point, 16.0f, 12.0f, 12.0f)) continue;
        // Each mine blinks on its own beat, so a minefield twinkles.
        const uint32_t beat = tick + (hashPosition(shot.pos) >> 10);
        const bool lit = !effects::flickerAllowed() || beat % 40u < 12u;
        spriteQuad(itemSprite(lit ? Item::MineOn : Item::MineOff), roundToInt(point.x), roundToInt(point.y), false, shot.evolved ? kRowEvolved : kRowBase, 0);
    }

    for (const Gem& gem : state.gems) {
        const ScreenPoint point = project(view, lerp(gem.prevPos, gem.pos, alpha));
        if (!onTarget(view, point, 10.0f, 16.0f, 8.0f)) continue;
        // Bigger XP makes a bigger, differently colored gem, so its worth is clear.
        const Item item = gem.xp >= 20 ? Item::GemHuge : (gem.xp >= 5 ? Item::GemLarge : Item::GemSmall);
        const int x = roundToInt(point.x);
        const int y = roundToInt(point.y);
        spriteQuad(itemSprite(item), x, y, false, kRowBase, 0);
    }

    constexpr Item kPickupItems[static_cast<int>(PickupType::Count)] = {Item::PickupHeal, Item::PickupMagnet, Item::PickupBomb, Item::PickupAmmo};
    for (const Pickup& pickup : state.pickups) {
        const ScreenPoint ground = project(view, pickup.pos);
        if (!onTarget(view, ground, 16.0f, 30.0f, 10.0f)) continue;
        const uint32_t beat = tick + (hashPosition(pickup.pos) >> 12);
        const float bob = effects::reducedMotion() ? 0.0f : triangleWave(beat, 90u);
        const int x = roundToInt(ground.x);
        const int y = roundToInt(ground.y);
        spriteQuad(itemSprite(bob > 0.5f ? Item::ShadowSmall : Item::ShadowMedium), x, y, false, kRowShadow, 0);
        spriteQuad(itemSprite(kPickupItems[static_cast<int>(pickup.type)]), x, roundToInt(ground.y - kPickupHeight - kPickupBob * bob), false, kRowBase, 0);
    }
}

// --- Sprites above the figures -------------------------------------------------

void drawAirSprites(const View& view, const GameState& state, float alpha) {
    const uint32_t tick = state.run.tick;
    for (const Zone& zone : state.zones) {
        if (zone.kind != ZoneKind::Explosion) continue;
        const ScreenPoint point = project(view, zone.a);
        const float reach = zone.radius * kArtPerWorld * point.scale + 20.0f;
        if (onTarget(view, point, reach, reach, reach)) drawExplosionFire(view, zone);
    }
    for (const Projectile& shot : state.projectiles) {
        if (shot.kind == ProjectileKind::Flame) {
            const ScreenPoint ground = project(view, lerp(shot.prevPos, shot.pos, alpha));
            if (onTarget(view, ground, 16.0f, 30.0f, 16.0f)) drawFlame(shot, ground, tick);
        } else if (shot.kind == ProjectileKind::Grenade) {
            const ScreenPoint ground = project(view, lerp(shot.prevPos, shot.pos, alpha));
            if (!onTarget(view, ground, 16.0f, 70.0f, 10.0f)) continue;
            // The grenade really moves flat. A height from its fuse makes it look lobbed.
            const float t = clampf(shot.life / grenadeFuse(), 0.0f, 1.0f);
            const float height = 4.0f * kGrenadeArcHeight * t * (1.0f - t) + 4.0f;
            const int x = roundToInt(ground.x);
            spriteQuad(itemSprite(height > 20.0f ? Item::ShadowTiny : Item::ShadowSmall), x, roundToInt(ground.y), false, kRowShadow, 0);
            spriteQuad(itemSprite(Item::Grenade), x, roundToInt(ground.y - height * ground.scale), false, shot.evolved ? kRowEvolved : kRowBase, 0);
        } else if (shot.kind == ProjectileKind::Rocket) {
            const ScreenPoint ground = project(view, lerp(shot.prevPos, shot.pos, alpha));
            if (!onTarget(view, ground, 16.0f, 30.0f, 10.0f)) continue;
            const Vec2 dir = screenDirection(shot.vel);
            // Rockets carry a screen angle, so y flips: atan2 with up as positive.
            const float angle = std::atan2(-dir.y, dir.x);
            const int index = static_cast<int>(std::floor((angle / kTau + 1.0f) * static_cast<float>(kRocketAngles) + 0.5f)) % kRocketAngles;
            const int x = roundToInt(ground.x);
            const int y = roundToInt(ground.y - kShotHeight * ground.scale);
            // A small flame at the tail, flickering between the two smallest puffs.
            const uint32_t flick = effects::flickerAllowed() ? tick >> 1 : 0u;
            spriteQuad(puffSprite((flick & 1u) != 0u ? 3.0f : 5.0f, flick), roundToInt(ground.x - dir.x * 7.0f), roundToInt(ground.y - kShotHeight * ground.scale - dir.y * 7.0f), false, shot.evolved ? kRowEvolved : kRowFireHot, 0);
            spriteQuad(rocketSprite(index), x, y, false, shot.evolved ? kRowEvolved : kRowBase, 0);
        }
    }

    const WeaponSlot* blades = heldBlades(state);
    if (blades) {
        const balance::WeaponLevel stats = weapons::effectiveStats(state, *blades);
        const int count = std::max(1, stats.count);
        // The angle is the latest step's, so step it back to match interpolated positions.
        const float angle = state.run.bladeAngle - stats.speed * kDt * (1.0f - alpha);
        const Vec2 center = lerp(state.player.prevPos, state.player.pos, alpha);
        const int spin = static_cast<int>(state.run.tick >> 1) % kBladeFrames;
        for (int i = 0; i < count; ++i) {
            const float bladeAngle = angle + kTau * static_cast<float>(i) / static_cast<float>(count);
            const ScreenPoint ground = project(view, center + fromAngle(bladeAngle) * weapons::bladeOrbitRadius(i, stats));
            spriteQuad(itemSprite(Item::ShadowTiny), roundToInt(ground.x), roundToInt(ground.y), false, kRowShadow, 0);
            spriteQuad(bladeSprite((spin + i) % kBladeFrames), roundToInt(ground.x), roundToInt(ground.y - kBladeHeight * ground.scale), false, blades->evolved ? kRowEvolved : kRowBase, 0);
        }
    }
}

void drawEnemyBullets(const View& view, const GameState& state, float alpha) {
    const uint32_t tick = state.run.tick;
    const SpriteRef& a = itemSprite(Item::EnemyBulletA);
    const SpriteRef& b = itemSprite(Item::EnemyBulletB);
    const SpriteRef& frame = effects::flickerAllowed() && ((tick >> 3) & 1u) != 0u ? b : a;
    for (const EnemyBullet& bullet : state.enemyBullets) {
        const ScreenPoint ground = project(view, lerp(bullet.prevPos, bullet.pos, alpha));
        if (!onTarget(view, ground, 16.0f, 30.0f, 16.0f)) continue;
        const int x = roundToInt(ground.x);
        const int y = roundToInt(ground.y);
        // The mark shows where it really is, as the bullet flies at chest height.
        spriteQuad(itemSprite(Item::ShadowTiny), x, y, false, kRowShadow, 0);
        spriteQuad(frame, x, roundToInt(ground.y - kShotHeight * ground.scale), false, kRowBase, 0);
    }
}

// --- Smoke -------------------------------------------------------------------

namespace {

struct Puff {
    Vec2 world;
    uint32_t bornTick = 0;
    uint16_t lifeTicks = 0;
    uint8_t size = 0;
    bool used = false;
    bool light = false;
};

constexpr int kMaxPuffs = 180;
Puff g_puffs[kMaxPuffs];
int g_nextPuff = 0;
uint32_t g_lastSmokeTick = 0;

void addPuff(Vec2 world, uint32_t tick, int lifeTicks, int size, bool light) {
    Puff& puff = g_puffs[g_nextPuff];
    g_nextPuff = (g_nextPuff + 1) % kMaxPuffs;
    puff.world = world;
    puff.bornTick = tick;
    puff.lifeTicks = static_cast<uint16_t>(lifeTicks);
    puff.size = static_cast<uint8_t>(size);
    puff.used = true;
    puff.light = light;
}

}  // namespace

void updateSmoke(const GameState& state) {
    const uint32_t tick = state.run.tick;
    if (tick == g_lastSmokeTick) return;
    if (tick < g_lastSmokeTick) {  // A new run has started.
        for (Puff& puff : g_puffs) puff.used = false;
    }
    g_lastSmokeTick = tick;

    for (const Projectile& shot : state.projectiles) {
        if (shot.kind == ProjectileKind::Rocket && (tick & 1u) == 0u) {
            addPuff(shot.pos - normalizedOr(shot.vel, Vec2{1.0f, 0.0f}) * 10.0f, tick, 30, 4, true);
        }
    }
    for (const Zone& zone : state.zones) {
        if (zone.kind != ZoneKind::Explosion) continue;
        if (zone.maxLife - zone.life > kDt * 1.5f) continue;
        // A blast has just started: leave smoke hanging after the fire has gone.
        const uint32_t seed = hashPosition(zone.a);
        const float radius = std::max(10.0f, zone.maxRadius);
        for (int i = 0; i < 7; ++i) {
            const uint32_t h = scramble(seed + static_cast<uint32_t>(i) * 131u);
            const Vec2 offset = fromAngle(static_cast<float>(h & 255u) / 255.0f * kTau) * (radius * 0.6f * static_cast<float>((h >> 8) & 255u) / 255.0f);
            addPuff(zone.a + offset, tick, 40 + static_cast<int>((h >> 16) & 31u), 7 + static_cast<int>((h >> 20) & 7u), (h >> 24) & 1u);
        }
    }
}

void drawSmoke(const View& view, uint32_t tick) {
    for (Puff& puff : g_puffs) {
        if (!puff.used) continue;
        const uint32_t age = tick >= puff.bornTick ? tick - puff.bornTick : puff.lifeTicks;
        if (age >= puff.lifeTicks) {
            puff.used = false;
            continue;
        }
        const ScreenPoint point = project(view, puff.world);
        if (!onTarget(view, point, 20.0f, 60.0f, 20.0f)) continue;
        const float t = static_cast<float>(age) / static_cast<float>(puff.lifeTicks);
        const float size = static_cast<float>(puff.size) * 0.5f * (0.7f + 0.8f * t) * point.scale;
        const auto strength = static_cast<uint8_t>(190.0f * (1.0f - t));
        spriteQuad(puffSprite(size, puff.bornTick), roundToInt(point.x), roundToInt(point.y - (6.0f + 30.0f * t) * point.scale), false, puff.light ? kRowSmokeLight : kRowSmoke, 255, strength);
    }
}

}  // namespace ls::worldview
