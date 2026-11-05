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

namespace {

// The one place the player's new position is applied, for walking and dashing alike.
// Solid obstacles will be handled here later: stop or slide the move, and a dash is
// stopped or slid exactly the same way.
void applyMovement(Player& player, Vec2 delta) { player.pos += delta; }

// Starts a dash if one is allowed. The direction is the way he is moving, or the way
// he faces when standing still.
void tryStartDash(GameState& state, Vec2 move) {
    Player& player = state.player;
    if (player.dashCooldown > 0.0f || player.dashLeft > 0.0f || player.hp <= 0.0f) return;
    player.dashDir = lengthSq(move) > 1e-4f ? normalizedOr(move, player.facing) : player.facing;
    player.dashLeft = balance::kDashSeconds;
    player.dashCooldown = balance::kDashCooldownSeconds;
    player.dashInvulnerable = balance::kDashInvulnerableSeconds;
    pushEvent(state, EventType::PlayerDashed, player.pos);
}

}  // namespace

void update(GameState& state, float moveX, float moveY, bool dashPressed) {
    Player& player = state.player;
    player.prevPos = player.pos;

    Vec2 move{moveX, moveY};
    // Analog sticks can go below full speed, but diagonals must not go above it.
    const float lenSq = lengthSq(move);
    if (lenSq > 1.0f) move *= 1.0f / std::sqrt(lenSq);
    if (lenSq > 1e-4f) player.facing = normalizedOr(move, player.facing);

    if (dashPressed) tryStartDash(state, move);

    if (player.dashLeft > 0.0f) {
        // The last step covers only the time that is left, so the distance is exact.
        const float time = std::min(kDt, player.dashLeft);
        applyMovement(player, player.dashDir * (balance::kDashSpeed * time));
        player.dashLeft -= kDt;
        if (player.dashLeft < 1e-4f) player.dashLeft = 0.0f;
    } else {
        applyMovement(player, move * (player.stats.moveSpeed * kDt));
    }

    player.invulnerable = std::max(0.0f, player.invulnerable - kDt);
    player.dashInvulnerable = std::max(0.0f, player.dashInvulnerable - kDt);
    player.dashCooldown = std::max(0.0f, player.dashCooldown - kDt);
    player.hitFlash = std::max(0.0f, player.hitFlash - kDt);
}

void damage(GameState& state, float amount) {
    Player& player = state.player;
    if (player.invulnerable > 0.0f || player.dashInvulnerable > 0.0f || player.hp <= 0.0f) return;
    // Every hit on the player comes through here, so difficulty needs only this one line.
    const float scaled = amount * balance::difficultyTuning(state.run.difficulty).enemyDamage;
    const float taken = std::max(balance::kMinDamageTaken, scaled - player.stats.armor);
    player.hp -= taken;
    player.invulnerable = balance::kPlayerInvulnerableAfterHit;
    player.hitFlash = balance::kPlayerHitFlashSeconds;
    pushEvent(state, EventType::PlayerHit, player.pos, taken);
}

void heal(GameState& state, float amount) {
    Player& player = state.player;
    if (player.hp <= 0.0f) return;  // A lethal hit stands. See the death rule in sim::step.
    player.hp = std::min(player.maxHp, player.hp + amount);
}

}  // namespace ls::player
