#include "components/Enemies/Boss/Boss.h"

namespace ls::enemies {

Vec2 bossVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
