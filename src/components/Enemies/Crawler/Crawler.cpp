#include "components/Enemies/Crawler/Crawler.h"

namespace ls::enemies {

Vec2 crawlerVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(state.player.pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
