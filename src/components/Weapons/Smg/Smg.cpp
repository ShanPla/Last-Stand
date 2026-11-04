#include "components/Weapons/Smg/Smg.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"

#include <cmath>

namespace ls::weapons {

bool fireSmg(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.player.pos;
    const int target = enemies::nearest(state, origin, balance::kWeaponRange);
    if (target < 0) return false;

    const Vec2 aim = normalizedOr(state.enemies[target].pos - origin, state.player.facing);
    const float baseAngle = std::atan2(aim.y, aim.x);
    for (int i = 0; i < stats.count; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        // The scatter comes from the run's own random stream, so replays match.
        const float angle = baseAngle + state.rng.range(-balance::kSmgScatterRadians, balance::kSmgScatterRadians);
        shot->pos = origin;
        shot->prevPos = origin;
        shot->vel = fromAngle(angle) * stats.speed;
        shot->damage = stats.damage;
        shot->radius = balance::kBulletRadius;
        shot->life = stats.duration;
        shot->pierce = static_cast<int16_t>(stats.pierce);
        shot->kind = ProjectileKind::Bullet;
        shot->source = slot.id;
        shot->evolved = slot.evolved;
    }
    markFired(state, slot, aim);
    return true;
}

}  // namespace ls::weapons
