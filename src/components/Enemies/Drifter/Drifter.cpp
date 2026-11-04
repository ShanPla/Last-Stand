#include "components/Enemies/Drifter/Drifter.h"

namespace ls::enemies {

Vec2 drifterVelocity(const GameState& state, const Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
