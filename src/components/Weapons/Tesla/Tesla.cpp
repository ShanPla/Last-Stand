#include "components/Weapons/Tesla/Tesla.h"

#include "components/Enemies/Enemies.h"
#include "components/Weapons/Weapons.h"

#include <algorithm>

namespace ls::weapons {

namespace {

// Lightning is instant, so the zone only exists to be drawn.
void addLink(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats, Vec2 from, Vec2 to) {
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
    zone->owner = static_cast<uint8_t>(owner);
    zone->applied = true;
}

}  // namespace

bool fireTesla(GameState& state, int owner, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    const Vec2 origin = state.players[owner].pos;
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
        enemies::hit(state, current, stats.damage, slot.id, owner, Vec2{});
        hitIndices[hitCount++] = current;
        addLink(state, owner, slot, stats, from, pos);
        from = pos;
        current = nearestEnemyExcluding(state, pos, stats.area, hitIndices, hitCount);
    }
    // Tesla does not point a gun anywhere, so the aim is left as it was.
    markFired(state, owner, slot, state.players[owner].aim);
    return true;
}

}  // namespace ls::weapons
