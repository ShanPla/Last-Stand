#include "components/Player/Player.h"

#include "data/Balance.h"

#include <algorithm>

namespace ls::player {

void reset(GameState& state) {
    Player& player = state.player;
    player = Player{};
    player.maxHp = balance::kPlayerMaxHp;
    player.xpToNext = balance::xpForLevel(1);
}

void update(GameState& state, float moveX, float moveY) {
    Player& player = state.player;
    player.prevPos = player.pos;

    Vec2 move{moveX, moveY};
    // Analog sticks can go below full speed, but diagonals must not go above it.
    const float lenSq = lengthSq(move);
    if (lenSq > 1.0f) move *= 1.0f / std::sqrt(lenSq);
    if (lenSq > 1e-4f) player.facing = normalizedOr(move, player.facing);
    player.pos += move * (player.stats.moveSpeed * kDt);

    player.invulnerable = std::max(0.0f, player.invulnerable - kDt);
    player.hitFlash = std::max(0.0f, player.hitFlash - kDt);
}

void damage(GameState& state, float amount) {
    Player& player = state.player;
    if (player.invulnerable > 0.0f || player.hp <= 0.0f) return;
    // Every hit on the player comes through here, so difficulty needs only this one line.
    const float scaled = amount * balance::difficultyTuning(state.run.difficulty).enemyDamage;
    const float taken = std::max(balance::kMinDamageTaken, scaled - player.stats.armor);
    player.hp -= taken;
    player.invulnerable = balance::kPlayerInvulnerableAfterHit;
    player.hitFlash = 0.15f;
    pushEvent(state, EventType::PlayerHit, player.pos, taken);
}

void heal(GameState& state, float amount) {
    Player& player = state.player;
    player.hp = std::min(player.maxHp, player.hp + amount);
}

}  // namespace ls::player
