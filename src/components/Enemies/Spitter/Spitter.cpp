#include "components/Enemies/Spitter/Spitter.h"

namespace ls::enemies {

Vec2 spitterVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
