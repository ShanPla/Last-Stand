#include "components/Weapons/Blades/Blades.h"

#include "components/Enemies/Enemies.h"

#include <cmath>

namespace ls::weapons {

void updateBlades(GameState& state, const WeaponSlot& slot, const balance::WeaponLevel& stats) {
    RunState& run = state.run;
    run.bladeAngle += stats.speed * kDt;
    // Keep the angle small, so float precision does not drift on long runs.
    if (run.bladeAngle > kTau) run.bladeAngle -= kTau;

    const Vec2 center = state.player.pos;
    for (int blade = 0; blade < stats.count; ++blade) {
        const float angle = run.bladeAngle + kTau * static_cast<float>(blade) / static_cast<float>(stats.count);
        const Vec2 offset = fromAngle(angle) * stats.area;
        const Vec2 pos = center + offset;
        state.enemyGrid.query(pos, balance::kBladeRadius + balance::kLargestEnemyRadius, [&](int index) {
            Enemy& enemy = state.enemies[index];
            // The per-enemy cooldown stops one zombie being cut on every step.
            if (enemy.hp <= 0.0f || enemy.bladeCooldown > 0.0f) return false;
            const float touch = enemy.radius + balance::kBladeRadius;
            if (distanceSq(enemy.pos, pos) >= touch * touch) return false;
            const Vec2 push = normalizedOr(offset, Vec2{}) * balance::kShotKnockback;
            enemies::hit(state, index, stats.damage, slot.id, push);
            enemy.bladeCooldown = stats.cooldown;
            return false;
        });
    }
}

}  // namespace ls::weapons
