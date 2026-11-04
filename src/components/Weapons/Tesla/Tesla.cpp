#include "components/Weapons/Tesla/Tesla.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"

#include <algorithm>

namespace ls::weapons {

namespace {

// Lightning is instant, so the zone only exists to be drawn.
void addLink(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats, Vec2 from, Vec2 to) {
    Zone* zone = state.zones.add();
    if (!zone) return;
    zone->kind = ZoneKind::TeslaLink;
    zone->a = from;
    zone->b = to;
    zone->life = stats.duration;
    zone->maxLife = stats.duration;
    zone->damage = stats.damage;
    zone->source = slot.id;
    zone->evolved = slot.evolved;
    zone->applied = true;
}

}  // namespace

bool fireTesla(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.player.pos;
    int current = enemies::nearest(state, origin, balance::kWeaponRange);
    if (current < 0) return false;

    // Enemies already struck are remembered by index, which stays valid all step.
    // A small fixed array is enough, and keeps the heap out of the simulation.
    int hitIndices[balance::kMaxWeaponTargets];
    int hitCount = 0;
    const int jumps = std::min(stats.count, balance::kMaxWeaponTargets);
    Vec2 from = origin;
    while (current >= 0 && hitCount < jumps) {
        const Vec2 pos = state.enemies[current].pos;
        enemies::hit(state, current, stats.damage, slot.id, Vec2{});
        hitIndices[hitCount++] = current;
        addLink(state, slot, stats, from, pos);
        from = pos;
        current = nearestEnemyExcluding(state, pos, stats.area, hitIndices, hitCount);
    }
    // Tesla does not point a gun anywhere, so the aim is left as it was.
    markFired(state, slot, state.player.aim);
    return true;
}

}  // namespace ls::weapons
