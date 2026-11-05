#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

namespace ls::enemies {

// Stored in enemy.state. While walking, run.bossStep says which attack of the phase's
// cycle comes next. The renderer draws a charge-up for the three charge states.
enum BossState : uint8_t {
    kBossWalk = 0,
    kBossRingCharge = 1,    // Standing still before the bullet ring.
    kBossDashCharge = 2,    // Standing still, aim locked in enemy.dir.
    kBossDash = 3,
    kBossSpiralCharge = 4,  // Standing still before the spiral, its first aim in enemy.dir.
    kBossSpiral = 5,        // Walking and firing spiral volleys. run.bossRepeat are left.
    kBossTrail = 6,         // Walking and dropping trail bursts. run.bossRepeat are left.
};

// True while the boss stands still and shows a charge-up warning around itself.
inline bool bossCharging(const Enemy& boss) {
    return boss.state == kBossRingCharge || boss.state == kBossDashCharge || boss.state == kBossSpiralCharge;
}

// Updates the boss phase and HP bar, walks, and starts or runs its attacks.
Vec2 bossVelocity(GameState& state, Enemy& enemy);

// Starts this attack at once, as if the cycle had reached it. For tests and debug scenes.
void startBossAttack(GameState& state, Enemy& boss, balance::BossAttack attack);

// The attack the cycle starts next.
balance::BossAttack nextBossAttack(const GameState& state);

}  // namespace ls::enemies
