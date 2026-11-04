#pragma once

#include "core/GameState.h"

namespace ls::enemies {

// Stored in enemy.state. The first four values mean "walking, and this attack
// comes next". Walking states double as the attack cycle, so no extra counter is
// needed. The renderer draws a charge-up for kBossRingCharge and kBossDashCharge.
enum BossState : uint8_t {
    kBossNextCircles = 0,
    kBossNextLine = 1,
    kBossNextRing = 2,
    kBossNextDash = 3,
    kBossRingCharge = 4,  // Standing still before the bullet ring.
    kBossDashCharge = 5,  // Standing still, aim locked in enemy.dir.
    kBossDash = 6,
};

// Updates the boss phase and HP bar, walks, and starts or runs its attacks.
Vec2 bossVelocity(GameState& state, Enemy& enemy);

}  // namespace ls::enemies
