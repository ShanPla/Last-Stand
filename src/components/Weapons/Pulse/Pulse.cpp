#include "components/Weapons/Pulse/Pulse.h"

#include "components/Enemies/Enemies.h"

#include <cmath>

namespace ls::weapons {

void firePulse(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.player.pos;
    const int target = enemies::nearest(state, origin, balance::kWeaponRange);
    if (target < 0) return;

    const Vec2 aim = normalizedOr(state.enemies[target].pos - origin, state.player.facing);
    const float baseAngle = std::atan2(aim.y, aim.x);
    const float firstOffset = -0.5f * static_cast<float>(stats.count - 1) * balance::kShotSpreadRadians;
    for (int i = 0; i < stats.count; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) return;
        const float angle = baseAngle + firstOffset + static_cast<float>(i) * balance::kShotSpreadRadians;
        shot->pos = origin;
        shot->prevPos = origin;
        shot->vel = fromAngle(angle) * stats.speed;
        shot->damage = stats.damage;
        shot->radius = balance::kShotRadius;
        shot->life = stats.duration;
        shot->pierce = static_cast<int16_t>(stats.pierce);
        shot->kind = ProjectileKind::Shot;
        shot->source = slot.id;
        shot->evolved = slot.evolved;
    }
    pushEvent(state, EventType::WeaponFired, origin, 0.0f, static_cast<uint8_t>(slot.id));
}

}  // namespace ls::weapons
