#include "components/WorldView/Objects.h"

#include "components/Weapons/Weapons.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

constexpr int kManyShots = 1200;           // Above this, bullets drop to a single quad.
constexpr float kFlameHeight = 16.0f;
constexpr float kGrenadeArcHeight = 70.0f;
constexpr float kMineRadius = 12.0f;
constexpr float kBladeHeight = 18.0f;
constexpr float kPickupRadius = 17.0f;
constexpr float kPickupHeight = 16.0f;
constexpr float kPickupBob = 4.0f;
constexpr int kTeslaSegments = 7;
constexpr float kTeslaJitter = 16.0f;
constexpr int kTeslaRefreshTicks = 2;      // The lightning redraws its shape this often.
constexpr float kBossLineChevronSpacing = 120.0f;

float flameLifetime() { return balance::kWeaponLevels[static_cast<int>(WeaponId::Flamethrower)][0].duration; }
float grenadeFuse() { return balance::kWeaponLevels[static_cast<int>(WeaponId::GrenadeLauncher)][0].duration; }

// A smooth 0 to 1 to 0 pulse from a counter, with no trigonometry per object.
float triangleWave(uint32_t counter, uint32_t period) {
    const uint32_t t = counter % period;
    const float half = static_cast<float>(period) * 0.5f;
    return std::abs(static_cast<float>(t) - half) / half;
}

// --- Player shots ------------------------------------------------------------

void drawBullet(const Projectile& shot, Vec2 view, bool cheap) {
    const bool pellet = shot.radius < balance::kBulletRadius - 0.5f;
    const Vec2 dir = normalizedOr(project(shot.vel), Vec2{1.0f, 0.0f});
    const Vec2 side = perpendicular(dir);
    const float tailLength = shot.radius * (pellet ? 2.2f : 4.0f);
    const float halfWidth = shot.radius * (pellet ? 0.50f : 0.42f);
    const Color color = shot.evolved ? theme::kEvolvedAccent : (pellet ? theme::kPellet : theme::kPlayerShot);
    const Vec2 head = view + dir * (shot.radius * 0.9f);
    const Vec2 tail = view - dir * tailLength;
    gradientQuad(tail + side * (halfWidth * 0.4f), tail - side * (halfWidth * 0.4f), fade(color, 0.0f), head - side * halfWidth,
                 head + side * halfWidth, color);
    if (!cheap) {
        const Vec2 coreTail = view - dir * (tailLength * 0.3f);
        const float coreWidth = halfWidth * 0.5f;
        quad(coreTail + side * coreWidth, coreTail - side * coreWidth, head - side * coreWidth, head + side * coreWidth, theme::kShotCore);
    }
}

void drawFlame(const Projectile& shot, Vec2 view, uint32_t tick) {
    const float life = clampf(shot.life / flameLifetime(), 0.0f, 1.0f);
    const uint32_t seed = hashPosition(shot.prevPos) >> 4;
    // Flames grow and thin out as they die. Their colors stay cool, as all the
    // player's weapons do: warm colors are kept for things that can hurt the player.
    const float size = shot.radius * (1.25f - 0.65f * life) * (0.9f + 0.2f * triangleWave(tick + seed, 12u));
    const Color outer = shot.evolved ? theme::kFlameEvolved : theme::kFlame;
    oval(view, size, size * 0.86f, 6, fade(outer, 0.18f + 0.50f * life));
    oval(view, size * 0.58f, size * 0.50f, 6, fade(theme::kFlameCore, 0.15f + 0.75f * life));
}

void drawGrenade(const Projectile& shot, Vec2 ground, uint32_t tick) {
    // The grenade really moves flat. A height from its fuse makes it look lobbed.
    const float t = clampf(shot.life / grenadeFuse(), 0.0f, 1.0f);
    const float height = 4.0f * kGrenadeArcHeight * t * (1.0f - t) + 8.0f;
    const Vec2 air{ground.x, ground.y - height};
    oval(ground, shot.radius * (1.2f - height / kGrenadeArcHeight * 0.3f), shot.radius * 0.5f, 6, theme::kShadow);
    oval(air, shot.radius * 1.3f, shot.radius * 1.3f, 4, shot.evolved ? theme::kEvolvedAccent : theme::kGrenade);
    oval(air, shot.radius * 0.95f, shot.radius * 0.95f, 4, theme::kGrenadeFill);
    const bool blinkOn = (tick / 4u) % 2u == 0u;
    oval(air, shot.radius * 0.42f, shot.radius * 0.42f, 6, blinkOn ? theme::kMineLight : theme::kGrenade);
}

void drawRocket(const Projectile& shot, Vec2 view, uint32_t tick) {
    const Vec2 dir = normalizedOr(project(shot.vel), Vec2{1.0f, 0.0f});
    const Vec2 side = perpendicular(dir);
    const float size = shot.radius;
    const Vec2 tail = view - dir * (size * 1.6f);
    const Vec2 nose = view + dir * (size * 1.5f);
    const Color bodyColor = shot.evolved ? theme::kEvolvedAccent : theme::kRocketBody;

    // A flickering plasma trail: quads that fade toward the far end.
    const float flicker = 0.75f + 0.5f * triangleWave(tick + (hashPosition(shot.prevPos) >> 8), 6u);
    const Vec2 trailEnd = tail - dir * (size * 4.2f * flicker);
    gradientQuad(tail + side * (size * 0.72f), tail - side * (size * 0.72f), fade(theme::kRocketTrail, 0.9f), trailEnd, trailEnd,
                 fade(theme::kRocketTrail, 0.0f));
    const Vec2 coreEnd = tail - dir * (size * 2.4f * flicker);
    gradientQuad(tail + side * (size * 0.34f), tail - side * (size * 0.34f), theme::kRocketTrailCore, coreEnd, coreEnd,
                 fade(theme::kRocketTrailCore, 0.0f));

    // Fins, then a rounded body with a bright outline, in the usual two passes.
    quad(tail - dir * (size * 0.35f) + side * (size * 1.05f), tail + dir * (size * 0.9f) + side * (size * 0.5f),
         tail + dir * (size * 0.9f) - side * (size * 0.5f), tail - dir * (size * 0.35f) - side * (size * 1.05f), bodyColor);
    turnedOval(view, dir, size * 1.95f, size * 0.78f, 4, bodyColor);
    turnedOval(view, dir, size * 1.55f, size * 0.46f, 4, theme::kRocketFill);
    turnedOval(nose - dir * (size * 0.35f), dir, size * 0.42f, size * 0.30f, 6, theme::kMineLight);
}

// --- Zones -------------------------------------------------------------------

void drawExplosion(const Zone& zone) {
    const Vec2 center = project(zone.a);
    const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
    const float radius = std::max(1.0f, zone.radius);
    const Color color = zone.hostile ? theme::kHostileZone : (zone.evolved ? theme::kBlastEvolved : theme::kBlast);
    groundOval(center, radius, 2, fade(color, 0.40f * life));
    groundOval(center, radius * 0.55f, 4, fade(color, 0.45f * life));
    groundRing(center, radius, 3.0f + 5.0f * life, 2, fade(color, life));
    groundRing(center, radius * 0.72f, 1.5f + 2.0f * life, 2, fade(color, 0.55f * life));
}

void drawBossCircle(const Zone& zone, uint32_t tick) {
    const Vec2 center = project(zone.a);
    const float radius = zone.maxRadius > 0.0f ? zone.maxRadius : zone.radius;
    const float progress = zone.maxLife > 0.0f ? clampf(1.0f - zone.life / zone.maxLife, 0.0f, 1.0f) : 1.0f;
    // Pulses faster right before it lands, so the moment to leave is hard to miss.
    const float pulse = triangleWave(tick, progress > 0.75f ? 8u : 24u);
    groundOval(center, radius, 2, fade(theme::kHostileFill, 0.12f));
    groundOval(center, radius * progress, 2, fade(theme::kHostileFill, 0.30f + 0.20f * pulse));
    groundRing(center, radius * progress, 3.0f, 2, fade(theme::kHostileTeeth, 0.9f));
    groundRing(center, radius, 5.0f, 2, theme::kHostileZone);

    // Sharp teeth on the rim keep the shape a threat, even with no color at all.
    constexpr int kToothStep = 4;
    for (int i = 0; i < kCirclePoints; i += kToothStep) {
        const Vec2 unit = g_circle.points[i];
        const Vec2 next = g_circle.points[(i + kToothStep) % kCirclePoints];
        const Vec2 tipUnit = g_circle.points[(i + kToothStep / 2) % kCirclePoints];
        const float depth = radius * (0.86f - 0.04f * pulse);
        const Vec2 a = center + project(unit * radius);
        const Vec2 b = center + project(next * radius);
        const Vec2 tip = center + project(tipUnit * depth);
        quad(a, b, tip, tip, theme::kHostileTeeth);
    }
}

void drawBossLine(const Zone& zone, uint32_t tick) {
    const float progress = zone.maxLife > 0.0f ? clampf(1.0f - zone.life / zone.maxLife, 0.0f, 1.0f) : 1.0f;
    const Vec2 worldDir = normalizedOr(zone.b - zone.a, Vec2{1.0f, 0.0f});
    const Vec2 worldSide = perpendicular(worldDir) * zone.radius;
    const Vec2 a = project(zone.a);
    const Vec2 b = project(zone.b);
    const Vec2 side = project(worldSide);
    const Vec2 fillEnd = lerp(a, b, progress);
    const float pulse = triangleWave(tick, progress > 0.75f ? 8u : 24u);

    quad(a + side, a - side, b - side, b + side, fade(theme::kHostileFill, 0.12f));
    quad(a + side, a - side, fillEnd - side, fillEnd + side, fade(theme::kHostileFill, 0.32f + 0.20f * pulse));
    thickLine(a + side, b + side, 4.0f, theme::kHostileZone);
    thickLine(a - side, b - side, 4.0f, theme::kHostileZone);
    thickLine(a + side, a - side, 4.0f, theme::kHostileZone);
    thickLine(b + side, b - side, 4.0f, theme::kHostileZone);
    thickLine(fillEnd + side, fillEnd - side, 3.0f, theme::kHostileTeeth);

    // Sharp arrows along the strip, like the circle's teeth.
    const float worldLength = distance(zone.a, zone.b);
    const int marks = std::min(32, static_cast<int>(worldLength / kBossLineChevronSpacing));
    const Vec2 point = project(worldDir * (zone.radius * 1.3f));
    for (int i = 1; i <= marks; ++i) {
        const Vec2 center = a + project(worldDir * (kBossLineChevronSpacing * static_cast<float>(i) - zone.radius * 0.5f));
        const float t = static_cast<float>(i) / static_cast<float>(marks + 1);
        const Color color = t < progress ? theme::kHostileTeeth : fade(theme::kHostileZone, 0.55f);
        thickLine(center + side * 0.75f, center + point, 3.5f, color);
        thickLine(center - side * 0.75f, center + point, 3.5f, color);
    }
}

void drawSniperBeam(const Zone& zone, float pixel) {
    const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
    const Vec2 lift{0.0f, -kShotHeight};
    const Vec2 a = project(zone.a) + lift;
    const Vec2 b = project(zone.b) + lift;
    const Color color = zone.evolved ? theme::kEvolvedAccent : theme::kBeam;
    thickLine(a, b, zone.radius * 2.0f * life + pixel, fade(color, 0.28f * life));
    thickLine(a, b, zone.radius * 0.8f * life + pixel, fade(color, 0.8f * life));
    thickLine(a, b, std::max(pixel * 1.5f, zone.radius * 0.28f * life), fade(theme::kBeamCore, life));
}

// A random-looking but repeatable zigzag between two points. Using tick and
// position here means the renderer never touches the simulation's random numbers.
void drawTeslaLink(const Zone& zone, uint32_t tick, float pixel) {
    const float life = zone.maxLife > 0.0f ? clampf(zone.life / zone.maxLife, 0.0f, 1.0f) : 0.0f;
    const Vec2 lift{0.0f, -kShotHeight};
    const Vec2 a = project(zone.a) + lift;
    const Vec2 b = project(zone.b) + lift;
    const Vec2 normal = perpendicular(normalizedOr(b - a, Vec2{1.0f, 0.0f}));
    const uint32_t seed = hashPosition(zone.a) ^ (tick / kTeslaRefreshTicks) * 2654435761u;
    const Color color = zone.evolved ? theme::kEvolvedAccent : theme::kTesla;

    Vec2 points[kTeslaSegments + 1];
    for (int i = 0; i <= kTeslaSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kTeslaSegments);
        const bool end = i == 0 || i == kTeslaSegments;
        const uint32_t random = scramble(seed + static_cast<uint32_t>(i));
        const float offset = end ? 0.0f : (static_cast<float>(random & 255u) / 127.5f - 1.0f) * kTeslaJitter;
        points[i] = lerp(a, b, t) + normal * offset;
    }
    for (int i = 0; i < kTeslaSegments; ++i) {
        thickLine(points[i], points[i + 1], 7.0f * life + pixel, fade(color, 0.30f * life));
    }
    for (int i = 0; i < kTeslaSegments; ++i) {
        thickLine(points[i], points[i + 1], std::max(pixel * 1.5f, 2.6f * life), fade(theme::kTeslaCore, life));
    }
    oval(b, 7.0f * life + 1.0f, 7.0f * life + 1.0f, 6, fade(color, 0.7f * life));
}

// --- Rewards -----------------------------------------------------------------

void drawHealIcon(Vec2 c, float r) {
    const float arm = r * 0.60f;
    const float half = r * 0.20f;
    quad(Vec2{c.x - arm, c.y - half}, Vec2{c.x + arm, c.y - half}, Vec2{c.x + arm, c.y + half}, Vec2{c.x - arm, c.y + half}, theme::kPickupIcon);
    quad(Vec2{c.x - half, c.y - arm}, Vec2{c.x + half, c.y - arm}, Vec2{c.x + half, c.y + arm}, Vec2{c.x - half, c.y + arm}, theme::kPickupIcon);
}

void drawMagnetIcon(Vec2 c, float r) {
    // A horseshoe: the lower half of a thick ring, plus two straight arms upward.
    const float outer = r * 0.62f;
    const float inner = r * 0.28f;
    const Vec2 ringCenter{c.x, c.y + r * 0.02f};
    constexpr int kStep = 4;
    // Circle index 0 points right, and 12 points down the screen, so 0 to 24 is the lower half.
    for (int i = 0; i < 24; i += kStep) {
        const Vec2 a = g_circle.points[i];
        const Vec2 b = g_circle.points[i + kStep];
        quad(ringCenter + a * inner, ringCenter + a * outer, ringCenter + b * outer, ringCenter + b * inner, theme::kPickupIcon);
    }
    const float tipTop = ringCenter.y - r * 0.50f;
    quad(Vec2{ringCenter.x + inner, tipTop}, Vec2{ringCenter.x + outer, tipTop}, Vec2{ringCenter.x + outer, ringCenter.y},
         Vec2{ringCenter.x + inner, ringCenter.y}, theme::kPickupIcon);
    quad(Vec2{ringCenter.x - outer, tipTop}, Vec2{ringCenter.x - inner, tipTop}, Vec2{ringCenter.x - inner, ringCenter.y},
         Vec2{ringCenter.x - outer, ringCenter.y}, theme::kPickupIcon);
    // Darker tips: every horseshoe magnet in a cartoon has them.
    const float tipHeight = r * 0.18f;
    quad(Vec2{ringCenter.x + inner, tipTop}, Vec2{ringCenter.x + outer, tipTop}, Vec2{ringCenter.x + outer, tipTop + tipHeight},
         Vec2{ringCenter.x + inner, tipTop + tipHeight}, theme::kPickupMagnet);
    quad(Vec2{ringCenter.x - outer, tipTop}, Vec2{ringCenter.x - inner, tipTop}, Vec2{ringCenter.x - inner, tipTop + tipHeight},
         Vec2{ringCenter.x - outer, tipTop + tipHeight}, theme::kPickupMagnet);
}

void drawBombIcon(Vec2 c, float r, uint32_t tick) {
    const Vec2 body{c.x - r * 0.06f, c.y + r * 0.12f};
    oval(body, r * 0.50f, r * 0.50f, 4, theme::kPickupIcon);
    oval(body + Vec2{-r * 0.14f, -r * 0.14f}, r * 0.13f, r * 0.13f, 12, theme::kPickupFill);
    const Vec2 fuseStart = body + Vec2{r * 0.30f, -r * 0.38f};
    const Vec2 fuseEnd = fuseStart + Vec2{r * 0.26f, -r * 0.26f};
    thickLine(fuseStart, fuseEnd, r * 0.13f, theme::kPickupIcon);
    const float spark = r * (0.12f + 0.12f * triangleWave(tick, 16u));
    oval(fuseEnd, spark, spark, 12, theme::kPickupBomb);
}

void drawAmmoIcon(Vec2 c, float r) {
    // A crate: a bright box, a dark inside, and a bullet standing in it.
    const float half = r * 0.60f;
    const float inner = half * 0.76f;
    quad(Vec2{c.x - half, c.y - half}, Vec2{c.x + half, c.y - half}, Vec2{c.x + half, c.y + half}, Vec2{c.x - half, c.y + half}, theme::kPickupIcon);
    quad(Vec2{c.x - inner, c.y - inner}, Vec2{c.x + inner, c.y - inner}, Vec2{c.x + inner, c.y + inner}, Vec2{c.x - inner, c.y + inner}, theme::kPickupFill);
    const float bullet = r * 0.17f;
    quad(Vec2{c.x - bullet, c.y - r * 0.12f}, Vec2{c.x + bullet, c.y - r * 0.12f}, Vec2{c.x + bullet, c.y + inner}, Vec2{c.x - bullet, c.y + inner}, theme::kPickupAmmo);
    quad(Vec2{c.x, c.y - inner}, Vec2{c.x + bullet, c.y - r * 0.12f}, Vec2{c.x - bullet, c.y - r * 0.12f}, Vec2{c.x, c.y - inner}, theme::kPickupIcon);
}

}  // namespace

void drawGroundZones(const GameState& state, const ViewBounds& view) {
    const uint32_t tick = state.run.tick;
    for (const Zone& zone : state.zones) {
        switch (zone.kind) {
            case ZoneKind::Explosion: {
                const float reach = zone.radius + 8.0f;
                if (view.contains(project(zone.a), reach, reach, reach)) drawExplosion(zone);
                break;
            }
            case ZoneKind::BossCircle: {
                const float reach = std::max(zone.radius, zone.maxRadius) + 8.0f;
                if (view.contains(project(zone.a), reach, reach, reach)) drawBossCircle(zone, tick);
                break;
            }
            case ZoneKind::BossLine: drawBossLine(zone, tick); break;
            default: break;
        }
    }
}

void drawMines(const GameState& state, float alpha, const ViewBounds& view) {
    const uint32_t tick = state.run.tick;
    for (const Projectile& shot : state.projectiles) {
        if (shot.kind != ProjectileKind::Mine) continue;
        const Vec2 center = project(lerp(shot.prevPos, shot.pos, alpha));
        if (!view.contains(center, kMineRadius * 3.0f, kMineRadius * 3.0f, kMineRadius * 3.0f)) continue;
        const Color color = shot.evolved ? theme::kEvolvedAccent : theme::kMine;
        // Faint ring: the distance at which a zombie sets it off.
        groundRing(center, balance::kMineTriggerRadius, 1.5f, 2, fade(color, 0.22f));
        groundOval(center, kMineRadius * 1.25f, 4, color);
        groundOval(center, kMineRadius * 0.95f, 4, theme::kMineFill);
        // Each mine blinks on its own beat, so a minefield twinkles.
        const uint32_t beat = tick + (hashPosition(shot.pos) >> 10);
        const bool lit = beat % 40u < 12u;
        groundOval(center, kMineRadius * 0.42f, 6, lit ? theme::kMineLight : fade(color, 0.45f));
    }
}

void drawGems(const GameState& state, float alpha, const ViewBounds& view) {
    for (const Gem& gem : state.gems) {
        const Vec2 center = project(lerp(gem.prevPos, gem.pos, alpha));
        if (!view.contains(center, 14.0f, 14.0f, 14.0f)) continue;
        // Bigger XP makes a bigger, differently colored gem, so its worth is clear.
        const float radius = clampf(4.5f + 1.3f * std::sqrt(static_cast<float>(gem.xp)), 5.5f, 12.0f);
        const Color color = gem.xp >= 20 ? theme::kGemHuge : (gem.xp >= 5 ? theme::kGemLarge : theme::kGem);
        const Vec2 raised{center.x, center.y - radius * 0.6f};
        oval(center, radius * 0.8f, radius * 0.32f, 12, theme::kShadow);
        oval(raised, radius, radius, 6, color);
        oval(raised, radius * 0.68f, radius * 0.68f, 6, theme::kGemFill);
        oval(raised + Vec2{-radius * 0.2f, -radius * 0.22f}, radius * 0.26f, radius * 0.26f, 12, theme::kGemShine);
    }
}

void drawPickups(const GameState& state, const ViewBounds& view) {
    const uint32_t tick = state.run.tick;
    for (const Pickup& pickup : state.pickups) {
        const Vec2 ground = project(pickup.pos);
        if (!view.contains(ground, kPickupRadius * 2.0f, kPickupRadius * 3.0f, kPickupRadius)) continue;
        Color color = theme::kPickupHeal;
        if (pickup.type == PickupType::Magnet) color = theme::kPickupMagnet;
        if (pickup.type == PickupType::Bomb) color = theme::kPickupBomb;
        if (pickup.type == PickupType::Ammo) color = theme::kPickupAmmo;

        const uint32_t beat = tick + (hashPosition(pickup.pos) >> 12);
        const float bob = triangleWave(beat, 90u);
        const Vec2 center{ground.x, ground.y - kPickupHeight - kPickupBob * bob};
        // The shadow shrinks as it rises, which sells the height.
        oval(ground, kPickupRadius * (0.85f - 0.08f * bob), kPickupRadius * (0.36f - 0.04f * bob), 6, theme::kShadow);
        groundRing(ground, kPickupRadius * 1.15f, 1.6f, 2, fade(color, 0.35f));
        oval(center, kPickupRadius, kPickupRadius, 4, color);
        oval(center, kPickupRadius * 0.84f, kPickupRadius * 0.84f, 4, theme::kPickupFill);
        switch (pickup.type) {
            case PickupType::Heal: drawHealIcon(center, kPickupRadius); break;
            case PickupType::Magnet: drawMagnetIcon(center, kPickupRadius); break;
            case PickupType::Bomb: drawBombIcon(center, kPickupRadius, beat); break;
            case PickupType::Ammo: drawAmmoIcon(center, kPickupRadius); break;
            case PickupType::Count: break;
        }
    }
}

void drawBlades(const GameState& state, float alpha) {
    const Player& player = state.player;
    const WeaponSlot* blades = nullptr;
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == WeaponId::Blades) blades = &player.weapons[i];
    }
    if (!blades) return;

    const balance::WeaponLevel stats = weapons::effectiveStats(state, *blades);
    const int count = std::max(1, stats.count);
    const float orbit = stats.area;
    // The angle is the latest step's, so step it back to match interpolated positions.
    const float angle = state.run.bladeAngle - stats.speed * kDt * (1.0f - alpha);
    const Vec2 center = lerp(player.prevPos, player.pos, alpha);
    const Color color = blades->evolved ? theme::kEvolvedAccent : theme::kBlade;
    const Vec2 lift{0.0f, -kBladeHeight};

    if (blades->evolved) groundRing(project(center), orbit, 3.0f, 2, fade(color, 0.35f));
    for (int i = 0; i < count; ++i) {
        const float bladeAngle = angle + kTau * static_cast<float>(i) / static_cast<float>(count);
        const Vec2 unit = fromAngle(bladeAngle);
        const Vec2 ground = project(center + unit * orbit);
        const Vec2 view = ground + lift;
        const Vec2 tangent = normalizedOr(project(Vec2{-unit.y, unit.x}), Vec2{1.0f, 0.0f});
        const Vec2 side = perpendicular(tangent);

        oval(ground, balance::kBladeRadius * 0.8f, balance::kBladeRadius * 0.3f, 6, theme::kShadow);
        // A fading trail behind the blade makes its spin direction readable.
        const float size = balance::kBladeRadius;
        const Vec2 tail = view - tangent * (size * 2.6f);
        gradientQuad(view + side * (size * 0.34f), view - side * (size * 0.34f), fade(color, 0.55f), tail, tail, fade(color, 0.0f));
        turnedOval(view, tangent, size * 1.05f, size * 0.46f, 4, color);
        turnedOval(view, tangent, size * 0.80f, size * 0.25f, 4, theme::kBladeFill);
        turnedOval(view + tangent * (size * 0.3f), tangent, size * 0.30f, size * 0.10f, 12, theme::kBladeCore);
    }
}

void drawPlayerShots(const GameState& state, float alpha, const ViewBounds& view) {
    const uint32_t tick = state.run.tick;
    const bool cheap = state.projectiles.count > kManyShots;
    const Vec2 lift{0.0f, -kShotHeight};
    for (const Projectile& shot : state.projectiles) {
        if (shot.kind == ProjectileKind::Mine) continue;
        const Vec2 ground = project(lerp(shot.prevPos, shot.pos, alpha));
        const float reach = shot.kind == ProjectileKind::Grenade ? kGrenadeArcHeight + 16.0f : 40.0f;
        if (!view.contains(ground, 40.0f, reach, 16.0f)) continue;
        switch (shot.kind) {
            case ProjectileKind::Bullet: drawBullet(shot, ground + lift, cheap); break;
            case ProjectileKind::Flame: drawFlame(shot, ground + Vec2{0.0f, -kFlameHeight}, tick); break;
            case ProjectileKind::Grenade: drawGrenade(shot, ground, tick); break;
            case ProjectileKind::Rocket: drawRocket(shot, ground + lift, tick); break;
            case ProjectileKind::Mine: break;
        }
    }
}

void drawBeams(const GameState& state, float pixel) {
    for (const Zone& zone : state.zones) {
        if (zone.kind == ZoneKind::SniperBeam) drawSniperBeam(zone, pixel);
        if (zone.kind == ZoneKind::TeslaLink) drawTeslaLink(zone, state.run.tick, pixel);
    }
}

void drawEnemyBullets(const GameState& state, float alpha, const ViewBounds& view) {
    const uint32_t tick = state.run.tick;
    const Vec2 lift{0.0f, -kShotHeight};
    for (const EnemyBullet& bullet : state.enemyBullets) {
        const Vec2 ground = project(lerp(bullet.prevPos, bullet.pos, alpha));
        const float radius = bullet.radius;
        if (!view.contains(ground, radius * 2.0f, kShotHeight + radius * 2.0f, radius * 2.0f)) continue;
        // The ground mark shows where it really is, as the bullet is drawn at chest height.
        oval(ground, radius * 0.95f, radius * 0.40f, 12, fade(theme::kEnemyBullet, 0.55f));

        // A sharp diamond, in three layers. The dark halo keeps the hot color
        // readable even when it passes over something equally bright.
        const Vec2 c = ground + lift;
        const float pulse = 1.0f + 0.14f * triangleWave(tick, 14u);
        const float halo = radius * 1.75f * pulse;
        const float body = radius * 1.40f * pulse;
        const float core = radius * 0.62f;
        const auto diamond = [c](float size, Color color) {
            quad(Vec2{c.x, c.y - size}, Vec2{c.x + size, c.y}, Vec2{c.x, c.y + size}, Vec2{c.x - size, c.y}, color);
        };
        diamond(halo, theme::kEnemyBulletHalo);
        diamond(body, theme::kEnemyBullet);
        diamond(core, theme::kEnemyBulletCore);
    }
}

}  // namespace ls::worldview
