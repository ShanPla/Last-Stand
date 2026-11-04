#include "components/Enemies/Brute/Brute.h"

namespace ls::enemies {

Vec2 bruteVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
