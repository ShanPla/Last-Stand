#include "components/Weapons/Rockets/Rockets.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"

#include <cmath>

namespace ls::weapons {

bool fireRockets(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.player.pos;
    const int target = enemies::nearest(state, origin, balance::kWeaponRange);
    if (target < 0) return false;

    const Vec2 aim = normalizedOr(state.enemies[target].pos - origin, state.player.facing);
    const float baseAngle = std::atan2(aim.y, aim.x);
    // The rockets leave in a fan, then curve in on their targets by themselves.
    const float firstOffset = -0.5f * static_cast<float>(stats.count - 1) * balance::kRocketFanStepRadians;
    for (int i = 0; i < stats.count; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        const float angle = baseAngle + firstOffset + static_cast<float>(i) * balance::kRocketFanStepRadians;
        shot->pos = origin;
        shot->prevPos = origin;
        shot->vel = fromAngle(angle) * stats.speed;
        shot->damage = stats.damage;
        shot->radius = balance::kRocketRadius;
        shot->area = stats.area;
        shot->life = stats.duration;
        shot->kind = ProjectileKind::Rocket;
        shot->source = slot.id;
        shot->evolved = slot.evolved;
    }
    markFired(state, slot, aim);
    return true;
}

}  // namespace ls::weapons
