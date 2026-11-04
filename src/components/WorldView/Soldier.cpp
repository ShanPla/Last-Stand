#include "components/WorldView/Characters.h"
#include "components/WorldView/Rig.h"

#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

// The soldier is built fresh each frame, since there is only one of him. Sizes are
// in figure units of one player radius: he stands 3 tall and 2 wide.
constexpr float kScale = balance::kPlayerRadius;
constexpr float kOutline = 2.0f / kScale;
constexpr float kShoulderHeight = 1.84f;
constexpr float kStrideLength = 58.0f;       // World units walked in one full walk cycle.
constexpr float kFlashSeconds = 0.07f;
constexpr float kRecoilSeconds = 0.12f;
constexpr float kRecoilDistance = 0.32f;     // How far the gun kicks back, in figure units.
constexpr float kBreathRate = 0.055f;        // Radians per simulation step.
constexpr float kReloadTilt = 0.85f;         // How far the barrel drops while reloading.
constexpr float kBlendRate = 12.0f;          // How fast animations ease in and out.
constexpr int kBlinkPeriodTicks = 6;         // Invulnerable blink, in simulation steps.
constexpr int kMaxSoldierQuads = 128;

struct SoldierAnimation {
    float walkDistance = 0.0f;
    float walkBlend = 0.0f;   // 0 when standing, 1 at a full walk.
    float reloadBlend = 0.0f; // 0 when aiming, 1 with the gun lowered.
    float flash = 0.0f;       // Seconds of muzzle flash left.
    float recoil = 0.0f;      // Seconds of recoil left.
    float magazineTime = 0.0f;
    Vec2 lastFeet{};
    WeaponId heldGun = WeaponId::Rifle;
    bool heldGunEvolved = false;
    bool started = false;
};

SoldierAnimation g_anim;
FigureQuad g_quads[kMaxSoldierQuads];

// The gun is built along a direction, in figure space, with its own forward and up.
struct GunFrame {
    Vec2 grip;
    Vec2 forward;
    Vec2 up;
    Vec2 at(float along, float above) const { return grip + forward * along + up * above; }
};

// Each gun has its own outline, so the held weapon can be told apart at a glance.
void buildGun(Rig& rig, const GunFrame& g, WeaponId gun, bool reloading, float magazineDrop) {
    const auto magazine = [&](float along, float length) {
        if (reloading) rig.limb(g.at(along, -0.08f - magazineDrop), g.at(along - 0.05f, -length - magazineDrop), 0.07f, 0.07f);
        else rig.limb(g.at(along, -0.08f), g.at(along - 0.05f, -length), 0.07f, 0.07f);
    };
    // These guns show no magazine while firing, but one appears as they reload, so
    // every gun that uses ammo has the same reload motion.
    const bool magazineOnlyOnReload = gun == WeaponId::Shotgun || gun == WeaponId::Flamethrower ||
                                      gun == WeaponId::GrenadeLauncher || gun == WeaponId::Rockets;
    if (reloading && magazineOnlyOnReload) magazine(0.10f, 0.36f);
    switch (gun) {
        case WeaponId::Shotgun:
            rig.limb(g.at(-0.62f, -0.10f), g.at(-0.20f, 0.0f), 0.12f, 0.10f);
            rig.limb(g.at(-0.20f, 0.0f), g.at(0.42f, 0.0f), 0.15f, 0.14f);
            rig.limb(g.at(0.42f, 0.05f), g.at(1.08f, 0.05f), 0.08f, 0.08f);
            rig.limb(g.at(0.42f, -0.10f), g.at(1.08f, -0.10f), 0.08f, 0.08f);
            rig.detailLimb(g.at(0.42f, -0.10f), g.at(0.72f, -0.10f), 0.10f, 0.10f);
            break;
        case WeaponId::Smg:
            magazine(0.10f, 0.58f);
            rig.limb(g.at(-0.26f, 0.0f), g.at(0.42f, 0.0f), 0.14f, 0.13f);
            rig.limb(g.at(0.42f, 0.02f), g.at(0.72f, 0.02f), 0.05f, 0.05f);
            break;
        case WeaponId::Sniper:
            magazine(-0.02f, 0.30f);
            rig.limb(g.at(-0.68f, -0.08f), g.at(-0.10f, 0.0f), 0.12f, 0.09f);
            rig.limb(g.at(-0.10f, 0.0f), g.at(0.70f, 0.0f), 0.09f, 0.07f);
            rig.limb(g.at(0.70f, 0.0f), g.at(1.75f, 0.0f), 0.04f, 0.04f);
            rig.limb(g.at(0.05f, 0.24f), g.at(0.52f, 0.24f), 0.07f, 0.07f);
            rig.detailLimb(g.at(0.45f, 0.24f), g.at(0.52f, 0.24f), 0.07f, 0.07f);
            break;
        case WeaponId::Flamethrower:
            rig.blob(g.at(-0.30f, 0.26f), 0.26f, 0.26f, 8);
            rig.limb(g.at(-0.12f, 0.0f), g.at(0.72f, 0.0f), 0.13f, 0.09f);
            rig.limb(g.at(0.72f, 0.0f), g.at(1.02f, 0.0f), 0.10f, 0.18f);
            rig.detailBlob(g.at(1.02f, 0.0f), 0.08f, 0.14f, 6, kLayerHot);
            break;
        case WeaponId::GrenadeLauncher:
            rig.limb(g.at(-0.44f, -0.06f), g.at(-0.10f, 0.0f), 0.11f, 0.11f);
            rig.blob(g.at(0.12f, -0.10f), 0.24f, 0.24f, 8);
            rig.limb(g.at(0.05f, 0.04f), g.at(0.86f, 0.04f), 0.20f, 0.20f);
            rig.detailLimb(g.at(0.72f, -0.16f), g.at(0.72f, 0.24f), 0.04f, 0.04f);
            break;
        case WeaponId::Rockets:
            rig.limb(g.at(-0.70f, 0.14f), g.at(-0.50f, 0.14f), 0.27f, 0.20f);
            rig.limb(g.at(-0.50f, 0.14f), g.at(0.86f, 0.14f), 0.20f, 0.20f);
            rig.limb(g.at(0.86f, 0.14f), g.at(1.02f, 0.14f), 0.20f, 0.26f);
            rig.detailBlob(g.at(0.98f, 0.14f), 0.08f, 0.15f, 6, kLayerHot);
            break;
        case WeaponId::Tesla:
            rig.limb(g.at(-0.30f, 0.0f), g.at(0.42f, 0.0f), 0.15f, 0.13f);
            rig.limb(g.at(0.40f, 0.14f), g.at(0.98f, 0.20f), 0.05f, 0.03f);
            rig.limb(g.at(0.40f, -0.14f), g.at(0.98f, -0.20f), 0.05f, 0.03f);
            rig.detailLimb(g.at(0.05f, -0.17f), g.at(0.05f, 0.17f), 0.04f, 0.04f);
            rig.detailLimb(g.at(0.22f, -0.16f), g.at(0.22f, 0.16f), 0.04f, 0.04f);
            rig.detailBlob(g.at(0.92f, 0.0f), 0.08f, 0.08f, 4, kLayerHot);
            break;
        default:  // Rifle, and the fallback before anything has fired.
            magazine(0.08f, 0.40f);
            rig.limb(g.at(-0.68f, -0.07f), g.at(-0.28f, 0.0f), 0.11f, 0.09f);
            rig.limb(g.at(-0.28f, 0.0f), g.at(0.62f, 0.0f), 0.12f, 0.10f);
            rig.limb(g.at(0.62f, 0.02f), g.at(1.18f, 0.02f), 0.05f, 0.05f);
            break;
    }
}

const WeaponSlot* slotFor(const Player& player, WeaponId id) {
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == id) return &player.weapons[i];
    }
    return nullptr;
}

// Mirrors to face the aim. Near-vertical shots keep the walking direction, so he
// doesn't flip back and forth while shooting straight up or down.
float mirrorSign(const Player& player) {
    const float x = std::abs(player.aim.x) > 0.05f ? player.aim.x : player.facing.x;
    return x < 0.0f ? -1.0f : 1.0f;
}

// The aim in figure space. The aim is a ground direction, so it is tilted first.
Vec2 figureAim(const Player& player, float sign) {
    const Vec2 view = normalizedOr(project(player.aim), Vec2{1.0f, 0.0f});
    return {view.x * sign, -view.y};
}

float fireRecoil() { return clampf(g_anim.recoil / kRecoilSeconds, 0.0f, 1.0f); }

// Gun direction once the reload tilt is applied, for the gun and the flash alike.
Vec2 gunDirection(const Player& player, float sign) {
    const Vec2 aim = figureAim(player, sign);
    const Vec2 lowered = normalizedOr(Vec2{0.45f, -1.0f}, aim);
    return normalizedOr(lerp(aim, lowered, g_anim.reloadBlend * kReloadTilt), aim);
}

Vec2 gunGrip(const Vec2& forward, float bodyDrop) {
    const Vec2 shoulder{0.12f, kShoulderHeight - bodyDrop};
    return shoulder + forward * (0.72f - kRecoilDistance * fireRecoil());
}

float muzzleLength(WeaponId gun) {
    switch (gun) {
        case WeaponId::Shotgun: return 1.12f;
        case WeaponId::Smg: return 0.76f;
        case WeaponId::Sniper: return 1.80f;
        case WeaponId::Flamethrower: return 1.08f;
        case WeaponId::GrenadeLauncher: return 0.90f;
        case WeaponId::Rockets: return 1.06f;
        case WeaponId::Tesla: return 1.02f;
        default: return 1.22f;
    }
}

float bodyDrop(const GameState& state) {
    const float breath = std::sin(static_cast<float>(state.run.tick) * kBreathRate);
    const float walkBob = std::abs(std::sin(g_anim.walkDistance / kStrideLength * kTau));
    return lerpf(0.035f * (breath + 1.0f), 0.09f * walkBob, g_anim.walkBlend);
}

}  // namespace

void noteWeaponFired(WeaponId weapon) {
    // Mines are dropped and blades orbit by themselves, so neither is held.
    if (weapon == WeaponId::Blades || weapon == WeaponId::Landmines) return;
    g_anim.heldGun = weapon;
    g_anim.flash = kFlashSeconds;
    g_anim.recoil = kRecoilSeconds;
}

void updateSoldierAnimation(const GameState& state, Vec2 feetWorld, float realDt) {
    const Player& player = state.player;
    const Vec2 moved = feetWorld - g_anim.lastFeet;
    const float distance = length(moved);
    g_anim.lastFeet = feetWorld;
    // A big jump is a new run starting, not a step.
    if (g_anim.started && distance < 60.0f) {
        const float heading = moved.x * mirrorSign(player) < 0.0f ? -1.0f : 1.0f;
        g_anim.walkDistance += distance * heading;
    }
    g_anim.started = true;

    const float blendStep = std::min(1.0f, kBlendRate * realDt);
    g_anim.walkBlend = lerpf(g_anim.walkBlend, distance > 0.01f ? 1.0f : 0.0f, blendStep);
    const WeaponSlot* slot = slotFor(player, g_anim.heldGun);
    const bool reloading = slot && slot->reloadLeft > 0.0f;
    g_anim.heldGunEvolved = slot && slot->evolved;
    g_anim.reloadBlend = lerpf(g_anim.reloadBlend, reloading ? 1.0f : 0.0f, blendStep);
    if (reloading) g_anim.magazineTime += realDt;
    g_anim.flash = std::max(0.0f, g_anim.flash - realDt);
    g_anim.recoil = std::max(0.0f, g_anim.recoil - realDt);
}

void drawSoldierShadow(Vec2 feet) {
    const float width = balance::kPlayerRadius * 1.25f;
    oval(feet, width * 1.3f, width * 0.55f, 4, fade(theme::kShadow, 0.5f));
    oval(feet, width, width * 0.42f, 4, theme::kShadow);
    // A faint ring on the ground marks where the player really is in a crowd.
    groundRing(feet, balance::kPlayerRadius * 1.25f, 1.8f, 2, theme::kPlayerRing);
}

void drawSoldier(const GameState& state, Vec2 feet) {
    const Player& player = state.player;
    // During invulnerable time he blinks, so the safe window can be seen.
    if (player.invulnerable > 0.0f && (state.run.tick / kBlinkPeriodTicks) % 2u == 1u) return;

    const float sign = mirrorSign(player);
    const float drop = bodyDrop(state);
    const float phase = g_anim.walkDistance / kStrideLength * kTau;
    const float stride = 0.42f * g_anim.walkBlend;
    const float lift = 0.30f * g_anim.walkBlend;
    const float recoil = fireRecoil();
    const float bodyLean = -0.06f * recoil;

    const Vec2 hip{0.0f + bodyLean * 0.5f, 1.02f - drop};
    const Vec2 chest{0.02f + bodyLean, 1.48f - drop};
    const Vec2 head{0.06f + bodyLean, 2.40f - drop};

    const auto footAt = [&](float hipX, float legPhase) {
        return Vec2{hipX - std::cos(legPhase) * stride, std::max(0.0f, std::sin(legPhase)) * lift};
    };
    const Vec2 nearFoot = footAt(0.34f, phase);
    const Vec2 farFoot = footAt(-0.34f, phase + kPi);
    const Vec2 nearHip = hip + Vec2{0.24f, 0.0f};
    const Vec2 farHip = hip + Vec2{-0.24f, 0.0f};

    const auto leg = [](Rig& rig, Vec2 from, Vec2 foot) {
        const Vec2 knee{(from.x + foot.x) * 0.5f + 0.08f, (from.y + foot.y) * 0.5f};
        rig.limb(from, knee, 0.19f, 0.16f);
        rig.limb(knee, foot + Vec2{0.0f, 0.12f}, 0.16f, 0.14f);
        rig.blob(foot + Vec2{0.10f, 0.10f}, 0.26f, 0.13f, 8);
    };

    const bool reloading = g_anim.reloadBlend > 0.3f;
    const float magazineDrop = reloading ? 0.30f * std::abs(std::sin(g_anim.magazineTime * 5.0f)) : 0.0f;
    const Vec2 forward = gunDirection(player, sign);
    const GunFrame gun{gunGrip(forward, drop), forward, Vec2{-forward.y, forward.x}};
    const bool awayFromCamera = player.aim.y < -0.55f;
    const float faceTurn = clampf(player.aim.y, 0.0f, 1.0f);

    Rig rig(g_quads, kMaxSoldierQuads, kOutline);

    // Far leg, far arm, and the gun when it points away, all behind the body.
    leg(rig, farHip, farFoot);
    const Vec2 shoulderFar = Vec2{0.30f + bodyLean, kShoulderHeight - drop - 0.04f};
    rig.limb(shoulderFar, gun.at(0.42f, -0.08f), 0.13f, 0.11f);
    rig.endGroup();
    if (awayFromCamera) {
        buildGun(rig, gun, g_anim.heldGun, reloading, magazineDrop);
        rig.endGroup();
    }

    // Body, in round shapes: the player is never sharp, unlike the threats.
    leg(rig, nearHip, nearFoot);
    rig.blob(chest + Vec2{-0.50f, 0.14f}, 0.26f, 0.36f, 8);
    rig.blob(chest, 0.54f, 0.58f, 12);
    rig.detailBlob(chest + Vec2{-0.50f, 0.14f}, 0.13f, 0.21f, 6, kLayerHot);
    rig.detailLimb(chest + Vec2{-0.30f, -0.30f}, chest + Vec2{0.34f, -0.30f}, 0.05f, 0.05f);
    rig.endGroup();

    // A round helmet, with a visor that turns to show which way he looks. From
    // behind there is no visor at all, so aiming away reads as his back.
    rig.blob(head + Vec2{0.0f, 0.04f}, 0.60f, 0.54f, 12);
    const float visorCenter = lerpf(0.24f, 0.0f, faceTurn);
    if (!awayFromCamera) {
        rig.limb(head + Vec2{visorCenter - 0.34f, 0.32f}, head + Vec2{visorCenter + 0.50f, 0.28f}, 0.05f, 0.05f);
        rig.detailLimb(head + Vec2{visorCenter - 0.28f, 0.04f}, head + Vec2{visorCenter + 0.32f, 0.04f}, 0.15f, 0.15f);
    } else {
        rig.detailLimb(head + Vec2{-0.04f, 0.44f}, head + Vec2{-0.04f, -0.32f}, 0.05f, 0.05f, kLayerHot);
    }
    rig.endGroup();

    // Gun and near arm, in front of everything when pointing at the camera.
    if (!awayFromCamera) {
        buildGun(rig, gun, g_anim.heldGun, reloading, magazineDrop);
        rig.endGroup();
    }
    const Vec2 hand = gun.at(-0.08f, -0.08f);
    const Vec2 shoulderNear{-0.16f + bodyLean, kShoulderHeight - drop - 0.06f};
    const Vec2 elbow = (shoulderNear + hand) * 0.5f + Vec2{-0.04f, -0.16f};
    rig.limb(shoulderNear, elbow, 0.15f, 0.13f);
    rig.limb(elbow, hand, 0.13f, 0.11f);
    rig.blob(hand, 0.14f, 0.14f, 6);
    rig.endGroup();

    Palette palette{{theme::kPlayer, theme::kPlayerFill, theme::kPlayerVisor, theme::kPlayerPack}};
    if (g_anim.heldGunEvolved) palette.colors[kLayerHot] = theme::kEvolvedAccent;
    if (player.hitFlash > 0.0f) {
        palette.colors[kLayerOutline] = theme::kPlayerHit;
        palette.colors[kLayerFill] = theme::kPlayerHit;
    }
    emitQuads(g_quads, rig.count(), Placement{feet.x, feet.y, sign * kScale, kScale}, palette);
}

void drawMuzzleFlash(const GameState& state, Vec2 feet) {
    if (g_anim.flash <= 0.0f) return;
    const Player& player = state.player;
    const float sign = mirrorSign(player);
    const float drop = bodyDrop(state);
    const Vec2 forward = gunDirection(player, sign);
    const Vec2 tip = gunGrip(forward, drop) + forward * muzzleLength(g_anim.heldGun);
    // Back from figure space (y up) to the screen (y down).
    const Vec2 center{feet.x + tip.x * sign * kScale, feet.y - tip.y * kScale};
    const Vec2 dir{forward.x * sign, -forward.y};
    const Vec2 side = perpendicular(dir);
    const float t = g_anim.flash / kFlashSeconds;
    const float size = kScale * (0.32f + 0.26f * t);
    // A short star past the barrel's end: long along it, short across it. It stays
    // small, so the flash doesn't hide the gun it comes from.
    turnedOval(center + dir * (size * 1.1f), dir, size * 1.2f, size * 0.42f, 6, fade(theme::kMuzzleFlash, 0.9f));
    turnedOval(center + dir * (size * 0.5f), side, size * 0.85f, size * 0.18f, 12, fade(theme::kMuzzleFlash, 0.9f));
    turnedOval(center + dir * (size * 0.9f), dir, size * 0.6f, size * 0.20f, 6, theme::kMuzzleCore);
}

}  // namespace ls::worldview
