#include "components/Weapons/GrenadeLauncher/GrenadeLauncher.h"

#include "components/Weapons/Weapons.h"

#include <algorithm>
#include <cmath>

namespace ls::weapons {

bool fireGrenadeLauncher(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.players[owner].pos;
    const float maxLob = stats.speed * stats.duration;
    const float reach = maxLob * balance::kShortRangeSlack;

    // Each grenade goes to a different zombie, nearest first.
    int chosen[balance::kMaxWeaponTargets];
    int chosenCount = 0;
    const int wanted = std::min(stats.count, balance::kMaxWeaponTargets);
    for (int i = 0; i < wanted; ++i) {
        const int target = nearestEnemyExcluding(state, origin, reach, chosen, chosenCount);
        if (target < 0) break;
        chosen[chosenCount++] = target;
    }
    if (chosenCount == 0) return false;

    Vec2 firstAim = state.players[owner].facing;
    for (int i = 0; i < stats.count; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        // With more grenades than targets, the extras reuse targets and scatter a little.
        const bool repeated = i >= chosenCount;
        const Vec2 toTarget = state.enemies[chosen[i % chosenCount]].pos - origin;
        Vec2 dir = normalizedOr(toTarget, state.players[owner].facing);
        if (repeated) {
            const float angle = std::atan2(dir.y, dir.x) +
                                state.rng.range(-balance::kGrenadeScatterRadians, balance::kGrenadeScatterRadians);
            dir = fromAngle(angle);
        }
        if (i == 0) firstAim = dir;
        // Speed is picked so the grenade arrives just as the fuse ends.
        const float lob = std::min(length(toTarget), maxLob);
        shot->pos = origin;
        shot->prevPos = origin;
        shot->vel = dir * (lob / stats.duration);
        shot->damage = stats.damage;
        shot->radius = balance::kGrenadeRadius;
        shot->area = stats.area;
        shot->life = stats.duration;
        shot->kind = ProjectileKind::Grenade;
        shot->source = slot.id;
        shot->evolved = slot.evolved;
        shot->owner = static_cast<uint8_t>(owner);
    }
    markFired(state, owner, slot, firstAim);
    return true;
}

}  // namespace ls::weapons
