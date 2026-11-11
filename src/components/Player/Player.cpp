#include "components/Player/Player.h"

#include "components/Obstacles/Obstacles.h"
#include "data/Balance.h"

#include <algorithm>

namespace ls::player {

void reset(GameState& state, int slot) {
    Player& player = state.players[slot];
    player = Player{};
    player.maxHp = balance::kPlayerMaxHp;
    player.xpToNext = balance::xpForLevel(1);
}

namespace {

// The one place a player's new position is applied, for walking and dashing alike. A car
// takes away only the part of the move going into it, so the rest slides along its side,
// and a dash is stopped or slid exactly the same way.
void applyMovement(GameState& state, Player& player, Vec2 delta) {
    player.pos = obstacles::sweepCircle(state, player.pos, player.pos + delta, balance::kPlayerRadius);
}

// Starts a dash if one is allowed. The direction is the way they are moving, or the way
// they face when standing still.
void tryStartDash(GameState& state, int slot, Vec2 move) {
    Player& player = state.players[slot];
    if (player.dashCooldown > 0.0f || player.dashLeft > 0.0f || player.hp <= 0.0f) return;
    player.dashDir = lengthSq(move) > 1e-4f ? normalizedOr(move, player.facing) : player.facing;
    player.dashLeft = balance::kDashSeconds;
    player.dashCooldown = balance::kDashCooldownSeconds;
    player.dashInvulnerable = balance::kDashInvulnerableSeconds;
    pushPlayerEvent(state, EventType::PlayerDashed, slot, player.pos);
}

}  // namespace

void update(GameState& state, int slot, float moveX, float moveY, bool dashPressed) {
    Player& player = state.players[slot];
    player.prevPos = player.pos;
    // The card shield only runs down while cards are open, and only in co-op, where it is set.
    // The last step's tiny float leftover is dropped, so the 5 seconds are never 5 and a bit.
    if (player.cardsOpen) {
        player.cardShield -= kDt;
        if (player.cardShield < 1e-4f) player.cardShield = 0.0f;
    }
    // A downed player lies still until a teammate gets them up.
    if (player.downed) return;

    Vec2 move{moveX, moveY};
    // Analog sticks can go below full speed, but diagonals must not go above it.
    const float lenSq = lengthSq(move);
    if (lenSq > 1.0f) move *= 1.0f / std::sqrt(lenSq);
    if (lenSq > 1e-4f) player.facing = normalizedOr(move, player.facing);

    if (dashPressed) tryStartDash(state, slot, move);

    if (player.dashLeft > 0.0f) {
        // The last step covers only the time that is left, so the distance is exact.
        const float time = std::min(kDt, player.dashLeft);
        applyMovement(state, player, player.dashDir * (balance::kDashSpeed * time));
        player.dashLeft -= kDt;
        if (player.dashLeft < 1e-4f) player.dashLeft = 0.0f;
    } else {
        applyMovement(state, player, move * (player.stats.moveSpeed * kDt));
    }

    player.invulnerable = std::max(0.0f, player.invulnerable - kDt);
    player.dashInvulnerable = std::max(0.0f, player.dashInvulnerable - kDt);
    player.dashCooldown = std::max(0.0f, player.dashCooldown - kDt);
    player.hitFlash = std::max(0.0f, player.hitFlash - kDt);
}

void damage(GameState& state, int slot, float amount) {
    Player& player = state.players[slot];
    if (player.invulnerable > 0.0f || player.dashInvulnerable > 0.0f || player.hp <= 0.0f) return;
    // Co-op: picking a card is safe, but only for kCoopCardShieldSeconds.
    if (player.cardsOpen && player.cardShield > 0.0f) return;
    // Every hit on a player comes through here, so difficulty needs only this one line.
    const float scaled = amount * balance::difficultyTuning(state.run.difficulty).enemyDamage;
    const float taken = std::max(balance::kMinDamageTaken, scaled - player.stats.armor);
    player.hp -= taken;
    player.invulnerable = balance::kPlayerInvulnerableAfterHit;
    player.hitFlash = balance::kPlayerHitFlashSeconds;
    pushPlayerEvent(state, EventType::PlayerHit, slot, player.pos, taken);
}

void heal(GameState& state, int slot, float amount) {
    Player& player = state.players[slot];
    if (player.hp <= 0.0f) return;  // A lethal hit stands. See the death rule in sim::step.
    player.hp = std::min(player.maxHp, player.hp + amount);
}

bool downAndRevive(GameState& state) {
    const int count = state.playerCount();
    // First everyone whose HP ran out this step goes down. A dash in progress stops there.
    for (int slot = 0; slot < count; ++slot) {
        Player& player = state.players[slot];
        if (player.downed || player.hp > 0.0f) continue;
        player.downed = true;
        player.hp = 0.0f;
        player.reviveProgress = 0.0f;
        player.dashLeft = 0.0f;
        player.dashInvulnerable = 0.0f;
        pushPlayerEvent(state, EventType::PlayerDowned, slot, player.pos);
    }
    // Then, if the whole team is down at once, the run is over and nobody can be revived.
    if (nearestStanding(state, Vec2{}) < 0) return true;

    // Contact time adds up while any standing teammate is close, and stays when they step
    // away, so a reviver who has to dodge for a moment doesn't lose what they have done.
    // Two teammates don't revive faster than one.
    const float reachSq = balance::kReviveRadius * balance::kReviveRadius;
    for (int slot = 0; slot < count; ++slot) {
        Player& player = state.players[slot];
        if (!player.downed) continue;
        bool helped = false;
        for (int other = 0; other < count && !helped; ++other) {
            helped = other != slot && standing(state, other) && distanceSq(state.players[other].pos, player.pos) <= reachSq;
        }
        if (!helped) continue;
        player.reviveProgress += kDt;
        if (player.reviveProgress < balance::kReviveSeconds - 1e-4f) continue;
        player.downed = false;
        player.reviveProgress = 0.0f;
        player.hp = player.maxHp * balance::kReviveHpShare;
        player.invulnerable = balance::kReviveInvulnerableSeconds;
        pushPlayerEvent(state, EventType::PlayerRevived, slot, player.pos);
    }
    return false;
}

}  // namespace ls::player
