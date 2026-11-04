#include "components/Enemies/Charger/Charger.h"

#include "data/Balance.h"

namespace ls::enemies {

Vec2 chargerVelocity(GameState& state, Enemy& enemy) {
    const Vec2 toPlayer = state.player.pos - enemy.pos;
    switch (enemy.state) {
        case kChargerChase:
            if (lengthSq(toPlayer) < balance::kChargerTriggerDistance * balance::kChargerTriggerDistance) {
                // The aim is locked now and never updated, so the player can dodge the dash.
                enemy.state = kChargerTelegraph;
                enemy.timer = balance::kChargerTelegraphSeconds;
                enemy.dir = normalizedOr(toPlayer, Vec2{1.0f, 0.0f});
                return Vec2{};
            }
            return normalizedOr(toPlayer, Vec2{}) * enemy.speed;
        case kChargerTelegraph:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                enemy.state = kChargerDash;
                enemy.timer = balance::kChargerDashSeconds;
            }
            return Vec2{};
        case kChargerDash:
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) {
                enemy.state = kChargerRecover;
                enemy.timer = balance::kChargerRecoverSeconds;
            }
            return enemy.dir * balance::kChargerDashSpeed;
        default:  // kChargerRecover, and any unknown value falls back to resting.
            enemy.timer -= kDt;
            if (enemy.timer <= 0.0f) enemy.state = kChargerChase;
            return Vec2{};
    }
}

}  // namespace ls::enemies
