#include "components/Enemies/Runner/Runner.h"

namespace ls::enemies {

Vec2 runnerVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
