#include "components/Enemies/Bloater/Bloater.h"

#include "components/Enemies/Enemies.h"

namespace ls::enemies {

Vec2 bloaterVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(targetOf(state, enemy).pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
