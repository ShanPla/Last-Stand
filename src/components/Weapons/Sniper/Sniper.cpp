#include "components/Weapons/Sniper/Sniper.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"

#include <cmath>

namespace ls::weapons {

namespace {

// Hurts every living zombie touching the segment, once.
void hitAlongBeam(GameState& state, Vec2 from, Vec2 to, float halfWidth, float damage, WeaponId source, int owner) {
    const Vec2 push = normalizedOr(to - from, Vec2{}) * balance::kShotKnockback;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        if (enemy.hp <= 0.0f) continue;
        const float reach = halfWidth + enemy.radius;
        if (distanceToSegmentSq(enemy.pos, from, to) >= reach * reach) continue;
        enemies::hit(state, i, damage, source, owner, push);
    }
}

}  // namespace

bool fireSniper(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.players[owner].pos;

    // The sniper picks the toughest target, since that is where its big hit matters most.
    int target = -1;
    float mostHp = 0.0f;
    const float rangeSq = balance::kWeaponRange * balance::kWeaponRange;
    for (int i = 0; i < state.enemies.count; ++i) {
        const Enemy& enemy = state.enemies[i];
        if (enemy.hp <= 0.0f || distanceSq(enemy.pos, origin) >= rangeSq) continue;
        if (enemy.hp > mostHp) {
            mostHp = enemy.hp;
            target = i;
        }
    }
    if (target < 0) return false;

    const Vec2 aim = normalizedOr(state.enemies[target].pos - origin, state.players[owner].facing);
    const float baseAngle = std::atan2(aim.y, aim.x);
    const float firstOffset = -0.5f * static_cast<float>(stats.count - 1) * balance::kSniperBeamSpreadRadians;
    for (int i = 0; i < stats.count; ++i) {
        const float angle = baseAngle + firstOffset + static_cast<float>(i) * balance::kSniperBeamSpreadRadians;
        const Vec2 end = origin + fromAngle(angle) * stats.speed;
        // The beam is instant, so it damages everything now and the zone is only for drawing.
        hitAlongBeam(state, origin, end, stats.area, stats.damage, slot.id, owner);
        if (Zone* zone = state.zones.add()) {
            zone->kind = ZoneKind::SniperBeam;
            zone->a = origin;
            zone->b = end;
            zone->radius = stats.area;
            zone->prevRadius = zone->radius;
            zone->maxRadius = zone->radius;
            zone->life = stats.duration;
            zone->maxLife = stats.duration;
            zone->damage = stats.damage;
            zone->source = slot.id;
            zone->evolved = slot.evolved;
            zone->owner = static_cast<uint8_t>(owner);
            zone->applied = true;
        }
    }
    markFired(state, owner, slot, aim);
    return true;
}

}  // namespace ls::weapons
