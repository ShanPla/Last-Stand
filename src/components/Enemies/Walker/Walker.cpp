#include "components/Enemies/Walker/Walker.h"

namespace ls::enemies {

Vec2 walkerVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
