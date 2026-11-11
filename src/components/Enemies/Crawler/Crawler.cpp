#include "components/Enemies/Crawler/Crawler.h"

#include "components/Enemies/Enemies.h"

namespace ls::enemies {

Vec2 crawlerVelocity(GameState& state, Enemy& enemy) {
    return normalizedOr(targetOf(state, enemy).pos - enemy.pos, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
