#include "components/Enemies/Runner/Runner.h"

#include "components/Enemies/Enemies.h"
#include "data/Balance.h"

#include <cmath>

namespace ls::enemies {

Vec2 runnerVelocity(GameState& state, Enemy& enemy) {
    const Vec2 forward = normalizedOr(targetOf(state, enemy).pos - enemy.pos, Vec2{});
    // The phase comes from the id and the tick, not from the random generator, so
    // every runner weaves out of step with its pack and the run stays replayable.
    const float seconds = static_cast<float>(state.run.tick) * kDt;
    const float phase = seconds * balance::kRunnerWeaveRadiansPerSecond +
                        static_cast<float>(enemy.id) * balance::kRunnerWeavePhasePerId;
    const Vec2 side{-forward.y, forward.x};
    const Vec2 wanted = forward + side * (std::sin(phase) * balance::kRunnerWeaveSpeedFraction);
    return normalizedOr(wanted, Vec2{}) * enemy.speed;
}

}  // namespace ls::enemies
