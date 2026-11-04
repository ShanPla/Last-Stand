#include "components/Enemies/Bloater/Bloater.h"

namespace ls::enemies {

Vec2 bloaterVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
