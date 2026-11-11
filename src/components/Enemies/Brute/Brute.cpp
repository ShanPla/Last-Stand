#include "components/Enemies/Brute/Brute.h"

#include "components/Enemies/Enemies.h"

namespace ls::enemies {

Vec2 bruteVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(targetOf(state, enemy).pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
