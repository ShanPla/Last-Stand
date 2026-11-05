#include "components/Weapons/Landmines/Landmines.h"

#include "components/Enemies/Enemies.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Weapons/Weapons.h"

#include <cmath>

namespace ls::weapons {

bool fireLandmines(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.player.pos;
    // Mines need no target, but laying them in an empty field would waste ammo.
    if (enemies::nearest(state, origin, balance::kWeaponRange) < 0) return false;

    // Zombies chase the player, so mines go on the side they are coming through.
    const Vec2 behind = state.player.facing * -1.0f;
    const float baseAngle = std::atan2(behind.y, behind.x);
    const float firstOffset = -0.5f * static_cast<float>(stats.count - 1) * balance::kMineSpreadRadians;
    for (int i = 0; i < stats.count; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        const float angle = baseAngle + firstOffset + static_cast<float>(i) * balance::kMineSpreadRadians;
        Vec2 pos = origin + fromAngle(angle) * balance::kMineDropDistance;
        obstacles::resolveCircle(state, pos, balance::kItemCarRadius);
        shot->pos = pos;
        shot->prevPos = pos;
        shot->vel = Vec2{};
        shot->damage = stats.damage;
        shot->radius = balance::kMineTriggerRadius;
        shot->area = stats.area;
        shot->life = stats.duration;
        shot->kind = ProjectileKind::Mine;
        shot->source = slot.id;
        shot->evolved = slot.evolved;
    }
    // A mine is dropped, not aimed, so the gun keeps pointing where it was.
    markFired(state, slot, state.player.aim);
    return true;
}

}  // namespace ls::weapons
