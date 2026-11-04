#include "components/Enemies/Charger/Charger.h"

namespace ls::enemies {

Vec2 chargerVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
