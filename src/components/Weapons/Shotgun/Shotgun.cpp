#include "components/Weapons/Shotgun/Shotgun.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"

#include <cmath>

namespace ls::weapons {

bool fireShotgun(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.player.pos;
    // Pellets die quickly, so a target past their reach is not worth a shell.
    const float reach = stats.speed * stats.duration * balance::kShortRangeSlack;
    const int target = enemies::nearest(state, origin, reach);
    if (target < 0) return false;

    const Vec2 aim = normalizedOr(state.enemies[target].pos - origin, state.player.facing);
    const float baseAngle = std::atan2(aim.y, aim.x);
    // Pellets are spread evenly from one edge of the cone to the other.
    const float step = stats.count > 1 ? balance::kShotgunConeRadians / static_cast<float>(stats.count - 1) : 0.0f;
    const float firstOffset = stats.count > 1 ? -0.5f * balance::kShotgunConeRadians : 0.0f;
    for (int i = 0; i < stats.count; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        const float angle = baseAngle + firstOffset + static_cast<float>(i) * step;
        shot->pos = origin;
        shot->prevPos = origin;
        shot->vel = fromAngle(angle) * stats.speed;
        shot->damage = stats.damage;
        shot->radius = balance::kPelletRadius;
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
