#include "Bot.h"

#include "components/Enemies/Boss/Boss.h"
#include "components/Enemies/Charger/Charger.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Weapons/Weapons.h"

#include <algorithm>
#include <cmath>

namespace ls::tools {

namespace {

// Tuned so each profile lands where its description says. See the notes in the
// report: these are approximations of human play.
const BotProfile kNovice = {
    "Novice",
    /*react*/ 12, 20,
    /*look*/ 230.0f, 8, 0.30f, false, false, 0.0f, 0.30f,
    /*mistakes*/ 0.50f, 0.12f,
    /*loot*/ 220.0f, 1.0f, false,
    /*dash*/ 0.15f, 120.0f, 4, 0.7f, false,  // Rarely, and late: only when hurt and swamped, and then not always.
    CardStyle::Random,
};

const BotProfile kAverage = {
    "Average",
    /*react*/ 5, 8,
    /*look*/ 380.0f, 12, 0.45f, true, false, 200.0f, 0.80f,
    /*mistakes*/ 0.20f, 0.03f,
    /*loot*/ 380.0f, 2.5f, true,
    /*dash*/ 0.5f, 60.0f, 3, 0.8f, false,  // Sometimes, to get out of a pile when it is hurting.
    CardStyle::Greedy,
};

const BotProfile kSkilled = {
    "Skilled",
    /*react*/ 2, 4,
    /*look*/ 520.0f, 20, 0.50f, true, true, 340.0f, 1.00f,
    /*mistakes*/ 0.04f, 0.0f,
    /*loot*/ 560.0f, 3.5f, true,
    /*dash*/ 1.0f, 30.0f, 4, 0.6f, true,  // Whenever it helps: warnings and shots, and a pile when it is hurting.
    CardStyle::Planner,
};

constexpr float kThreatFalloff = 45.0f;       // Danger fades over about this distance.
constexpr float kCrowdFalloff = 110.0f;       // Wider falloff for the "avoid crowds" term.
constexpr float kContactPenalty = 6.0f;       // Extra cost when a heading ends inside a zombie.
constexpr float kHazardPenalty = 40.0f;       // Cost of ending inside a boss zone.
constexpr float kHazardMargin = 10.0f;
constexpr float kMomentumWeight = 0.25f;      // Keeps the path smooth instead of jittering.
constexpr float kBulletFalloff = 25.0f;
constexpr float kDashContactRange = 30.0f;    // Gap to a zombie that counts as touching, for feeling surrounded.
constexpr float kDashWarnSeconds = 0.45f;     // A warning this close to going off is worth leaving.
constexpr float kDashBulletSeconds = 0.2f;    // A shot this close to landing is worth leaving.
constexpr float kDashPanicHp = 0.3f;          // Health share at which even a poor player reaches for the dash.
constexpr float kDashMinGain = 1.5f;          // The dash must make things at least this much better.
constexpr float kCarStallPenalty = 1.0f;      // Cost of a heading that a solid car would stop short.
constexpr float kChargerLaneLength = balance::kChargerDashSpeed * balance::kChargerDashSeconds;
constexpr float kBossDashLaneLength = balance::kBossDashSpeed * balance::kBossDashSeconds;

Vec2 rotated(Vec2 v, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

float segmentDistance(Vec2 point, Vec2 a, Vec2 b) {
    const Vec2 along = b - a;
    const float lenSq = lengthSq(along);
    if (lenSq < 1e-8f) return distance(point, a);
    const float t = clampf(dot(point - a, along) / lenSq, 0.0f, 1.0f);
    return distance(point, a + along * t);
}

const WeaponSlot* findWeapon(const Player& player, WeaponId id) {
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == id) return &player.weapons[i];
    }
    return nullptr;
}

const PassiveSlot* findPassive(const Player& player, PassiveId id) {
    for (int i = 0; i < player.passiveCount; ++i) {
        if (player.passives[i].id == id) return &player.passives[i];
    }
    return nullptr;
}

}  // namespace

const BotProfile& novice() { return kNovice; }
const BotProfile& average() { return kAverage; }
const BotProfile& skilled() { return kSkilled; }

Bot::Bot(const BotProfile& profile, uint64_t seed, int favoriteWeapon) : profile_(profile), favorite_(favoriteWeapon) {
    rng_.seed(seed * 0x9E3779B97F4A7C15ULL + 12345u);
    threats_.reserve(512);
    bullets_.reserve(256);
    hazards_.reserve(32);
}

InputFrame Bot::act(const GameState& state) {
    InputFrame input;
    if (state.run.phase == RunPhase::LevelUp) {
        decideCard(state, input);
        return input;
    }
    if (stepsUntilDecision_ <= 0) {
        decideMove(state);
        stepsUntilDecision_ = profile_.reactMinSteps + rng_.below(profile_.reactMaxSteps - profile_.reactMinSteps + 1);
    }
    --stepsUntilDecision_;
    input.moveX = heading_.x;
    input.moveY = heading_.y;
    input.dash = dashNext_;
    dashNext_ = false;
    return input;
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

void Bot::gatherThreats(const GameState& state) {
    threats_.clear();
    bullets_.clear();
    hazards_.clear();
    const Vec2 pos = state.player.pos;
    const float lookSq = profile_.lookRadius * profile_.lookRadius;
    const bool seesWarnings = rng_.chance(profile_.telegraphChance);

    for (const Enemy& enemy : state.enemies) {
        if (distanceSq(enemy.pos, pos) > lookSq) continue;
        Vec2 vel{};
        if (profile_.predictsMovement) {
            const bool charger = enemy.type == EnemyType::Charger;
            const bool boss = enemy.type == EnemyType::Boss;
            if (charger && enemy.state == enemies::kChargerDash) {
                vel = enemy.dir * balance::kChargerDashSpeed;
            } else if (boss && enemy.state == enemies::kBossDash) {
                vel = enemy.dir * balance::kBossDashSpeed;
            } else if (enemy.type == EnemyType::Spitter || (charger && enemy.state != enemies::kChargerChase) ||
                       (boss && enemy.state >= enemies::kBossRingCharge)) {
                vel = Vec2{};
            } else {
                vel = normalizedOr(pos - enemy.pos, Vec2{}) * enemy.speed;
            }
        }
        threats_.push_back({enemy.pos, vel, enemy.radius, enemy.contactDamage / 8.0f});

        // A charger's warning shows where it will dash. The Boss's dash warning works the same way.
        if (seesWarnings) {
            if (enemy.type == EnemyType::Charger && enemy.state == enemies::kChargerTelegraph) {
                hazards_.push_back({enemy.pos, enemy.pos + enemy.dir * kChargerLaneLength,
                                    enemy.radius + balance::kPlayerRadius, enemy.timer + balance::kChargerDashSeconds, 1.0f});
            } else if (enemy.type == EnemyType::Boss && enemy.state == enemies::kBossDashCharge) {
                hazards_.push_back({enemy.pos, enemy.pos + enemy.dir * kBossDashLaneLength,
                                    enemy.radius + balance::kPlayerRadius, enemy.timer + balance::kBossDashSeconds, 1.0f});
            }
        }
    }

    if (profile_.bulletSight > 0.0f) {
        for (const EnemyBullet& bullet : state.enemyBullets) {
            if (distanceSq(bullet.pos, pos) > profile_.bulletSight * profile_.bulletSight) continue;
            bullets_.push_back({bullet.pos, bullet.vel, bullet.radius, bullet.damage / 8.0f});
        }
    }

    if (seesWarnings) {
        for (const Zone& zone : state.zones) {
            if (!zone.hostile) continue;
            hazards_.push_back({zone.a, zone.kind == ZoneKind::BossLine ? zone.b : zone.a, zone.radius, zone.life,
                                zone.damage / 26.0f});
        }
    }
}

float Bot::ammoNeed(const GameState& state) const {
    if (!profile_.knowsAmmoNeed) return 0.3f;
    float missing = 0.0f;
    int guns = 0;
    for (int i = 0; i < state.player.weaponCount; ++i) {
        const WeaponSlot& slot = state.player.weapons[i];
        const balance::WeaponLevel stats = weapons::effectiveStats(state, slot);
        if (stats.magazine <= 0) continue;
        const float have = static_cast<float>(slot.magazine + slot.reserve);
        const float most = static_cast<float>(stats.magazine + stats.maxReserve);
        missing += 1.0f - have / most;
        ++guns;
    }
    return guns > 0 ? missing / static_cast<float>(guns) : 0.0f;
}

void Bot::chooseLoot(const GameState& state) {
    hasLoot_ = false;
    const Vec2 pos = state.player.pos;
    const float reach = profile_.lootRadius;
    float best = 0.0f;

    const float need = ammoNeed(state);
    const float hurt = 1.0f - state.player.hp / state.player.maxHp;
    for (const Pickup& pickup : state.pickups) {
        const float dist = distance(pickup.pos, pos);
        if (dist > reach * 1.5f) continue;
        float value = 5.0f;
        switch (pickup.type) {
            case PickupType::Ammo: value = 10.0f + 90.0f * need; break;
            case PickupType::Heal: value = 8.0f + 90.0f * hurt; break;
            case PickupType::Magnet: value = 10.0f + 0.05f * static_cast<float>(state.gems.count); break;
            case PickupType::Bomb: value = 12.0f + 0.03f * static_cast<float>(state.enemies.count); break;
            case PickupType::Count: break;
        }
        const float score = value / (dist + 120.0f);
        if (score > best) {
            best = score;
            lootTarget_ = pickup.pos;
            hasLoot_ = true;
        }
    }
    for (const Gem& gem : state.gems) {
        const float dist = distance(gem.pos, pos);
        if (dist > reach) continue;
        const float score = (1.0f + 0.6f * static_cast<float>(gem.xp)) / (dist + 120.0f);
        if (score > best) {
            best = score;
            lootTarget_ = gem.pos;
            hasLoot_ = true;
        }
    }
}

float Bot::headingCost(const GameState& state, Vec2 dir) const {
    const Vec2 pos = state.player.pos;
    const float speed = state.player.stats.moveSpeed;
    const float near = profile_.avoidsCrowds ? profile_.lookAheadSeconds * 0.5f : profile_.lookAheadSeconds;
    const float far = profile_.lookAheadSeconds * 1.6f;
    const float playerR = balance::kPlayerRadius;
    float cost = 0.0f;

    // A player sees the cars. Where a heading really ends is what counts, and walking
    // into the side of a car is a waste of time, not a way to get anywhere.
    const Vec2 wantedPos = pos + dir * (speed * near);
    const Vec2 nearPos = obstacles::sweepCircle(state, pos, wantedPos, playerR);
    cost += kCarStallPenalty * (1.0f - clampf(dot(nearPos - pos, dir) / std::max(speed * near, 1.0f), 0.0f, 1.0f));
    for (const Threat& threat : threats_) {
        const Vec2 q = threat.pos + threat.vel * near;
        const float gap = distance(nearPos, q) - threat.radius - playerR;
        cost += threat.weight * (gap < 0.0f ? kContactPenalty : std::exp(-gap / kThreatFalloff));
    }
    if (profile_.avoidsCrowds) {
        const Vec2 farPos = obstacles::sweepCircle(state, pos, pos + dir * (speed * far), playerR);
        for (const Threat& threat : threats_) {
            const Vec2 q = threat.pos + threat.vel * far;
            const float gap = distance(farPos, q) - threat.radius - playerR;
            cost += 0.4f * threat.weight * std::exp(-std::max(gap, 0.0f) / kCrowdFalloff);
        }
    }
    // Bullets are slow, so look at three moments ahead: a shot that is about to arrive is avoided early.
    for (const Threat& bullet : bullets_) {
        for (const float t : {0.15f, 0.35f, 0.6f}) {
            const Vec2 q = bullet.pos + bullet.vel * t;
            const Vec2 mine = pos + dir * (speed * t);
            const float gap = distance(mine, q) - bullet.radius - playerR;
            cost += bullet.weight * (gap < 0.0f ? kContactPenalty : 2.0f * std::exp(-gap / kBulletFalloff));
        }
    }
    for (const Hazard& hazard : hazards_) {
        const float t = clampf(hazard.life, 0.15f, 0.8f);
        const Vec2 where = pos + dir * (speed * t);
        const float gap = segmentDistance(where, hazard.a, hazard.b) - hazard.radius - playerR - kHazardMargin;
        cost += hazard.weight * (gap < 0.0f ? kHazardPenalty : 3.0f * std::exp(-gap / 30.0f));
    }

    if (hasLoot_) {
        // With a car in the way, the good heading is the one that goes around it.
        const Vec2 straight = normalizedOr(lootTarget_ - pos, dir) * speed;
        const Vec2 around = obstacles::steerAround(state, pos, playerR, straight, lootTarget_, 1u);
        if (around.x != straight.x || around.y != straight.y) {
            cost -= profile_.lootWeight * dot(dir, normalizedOr(around, dir));
        } else {
            const float before = distance(pos, lootTarget_);
            const float after = distance(nearPos, lootTarget_);
            cost -= profile_.lootWeight * clampf((before - after) / std::max(speed * near, 1.0f), -1.0f, 1.0f);
        }
    }
    cost += kMomentumWeight * (1.0f - dot(dir, heading_)) * 0.5f;
    return cost;
}

void Bot::decideMove(const GameState& state) {
    if (rng_.chance(profile_.wanderChance)) {
        heading_ = fromAngle(rng_.range(0.0f, kTau));
        return;
    }
    gatherThreats(state);
    chooseLoot(state);

    float bestCost = 1.0e30f;
    Vec2 bestDir = heading_;
    for (int k = 0; k < profile_.directions; ++k) {
        const Vec2 dir = fromAngle(kTau * static_cast<float>(k) / static_cast<float>(profile_.directions));
        const float cost = headingCost(state, dir);
        if (cost < bestCost) {
            bestCost = cost;
            bestDir = dir;
        }
    }
    heading_ = rotated(bestDir, rng_.range(-profile_.noiseRadians, profile_.noiseRadians));
    considerDash(state);
}

// How bad it is to be at a point once the given time has passed: the same idea as
// headingCost, but for a spot instead of a heading.
float Bot::dashCost(Vec2 where, float seconds) const {
    const float playerR = balance::kPlayerRadius;
    float cost = 0.0f;
    for (const Threat& threat : threats_) {
        const Vec2 q = threat.pos + threat.vel * seconds;
        const float gap = distance(where, q) - threat.radius - playerR;
        cost += threat.weight * (gap < 0.0f ? kContactPenalty : std::exp(-gap / kThreatFalloff));
    }
    for (const Threat& bullet : bullets_) {
        const Vec2 q = bullet.pos + bullet.vel * seconds;
        const float gap = distance(where, q) - bullet.radius - playerR;
        cost += bullet.weight * (gap < 0.0f ? kContactPenalty : 2.0f * std::exp(-gap / kBulletFalloff));
    }
    for (const Hazard& hazard : hazards_) {
        const float gap = segmentDistance(where, hazard.a, hazard.b) - hazard.radius - playerR - kHazardMargin;
        cost += hazard.weight * (gap < 0.0f ? kHazardPenalty : 3.0f * std::exp(-gap / 30.0f));
    }
    return cost;
}

// Decides whether to dash on the next step, and sets the heading to dash along. It only
// dashes when it has a reason (surrounded, or for the skilled bot a warning or a shot
// about to land) and the best landing spot is clearly better than staying put.
void Bot::considerDash(const GameState& state) {
    dashNext_ = false;
    if (profile_.dashChance <= 0.0f || state.timeSeconds() < profile_.dashAfterSeconds) return;
    const Player& player = state.player;
    if (player.dashCooldown > 0.0f || player.dashLeft > 0.0f || player.hp <= 0.0f) return;

    const Vec2 pos = player.pos;
    const float playerR = balance::kPlayerRadius;
    int touching = 0;
    for (const Threat& threat : threats_) {
        if (distance(threat.pos, pos) - threat.radius - playerR < kDashContactRange) ++touching;
    }
    bool reason = touching >= profile_.dashCrowd && player.hp < profile_.dashHurtBelow * player.maxHp;
    // Anyone panics when nearly dead with zombies on top of them.
    if (touching >= 2 && player.hp < kDashPanicHp * player.maxHp) reason = true;
    if (profile_.dashDodges && !reason) {
        for (const Hazard& hazard : hazards_) {
            if (hazard.life < kDashWarnSeconds && segmentDistance(pos, hazard.a, hazard.b) < hazard.radius + playerR + kHazardMargin) reason = true;
        }
        for (const Threat& bullet : bullets_) {
            const Vec2 q = bullet.pos + bullet.vel * kDashBulletSeconds;
            if (distance(pos, q) < bullet.radius + playerR + 12.0f) reason = true;
        }
    }
    if (!reason || !rng_.chance(profile_.dashChance)) return;

    const float here = dashCost(pos, balance::kDashSeconds);
    float bestCost = 1.0e30f;
    Vec2 bestDir = heading_;
    for (int k = 0; k < profile_.directions; ++k) {
        const Vec2 dir = fromAngle(kTau * static_cast<float>(k) / static_cast<float>(profile_.directions));
        const float cost = dashCost(obstacles::sweepCircle(state, pos, pos + dir * balance::kDashDistance, playerR), balance::kDashSeconds);
        if (cost < bestCost) {
            bestCost = cost;
            bestDir = dir;
        }
    }
    if (bestCost + kDashMinGain > here) return;
    heading_ = bestDir;
    dashNext_ = true;
}

// ---------------------------------------------------------------------------
// Level-up cards
// ---------------------------------------------------------------------------

// The evolution the bot is building toward: the recipe whose weapon is highest
// level, with its passive already held as the tie-break.
const balance::Evolution* Bot::targetRecipe(const GameState& state) const {
    const balance::Evolution* best = nullptr;
    int bestScore = -1;
    for (const balance::Evolution& evolution : balance::kEvolutions) {
        const WeaponSlot* weapon = findWeapon(state.player, evolution.weapon);
        if (weapon && weapon->evolved) continue;
        int score = static_cast<int>(evolution.weapon) == favorite_ ? 1000 : 0;  // A favorite decides the recipe.
        if (weapon) score += 10 * weapon->level;
        if (findPassive(state.player, evolution.passive)) score += 5;
        if (score > bestScore) {
            bestScore = score;
            best = &evolution;
        }
    }
    return best;
}

float Bot::scoreCard(const GameState& state, const Card& card) {
    const Player& player = state.player;
    const float jitter = rng_.range(0.0f, 3.0f);  // Breaks ties, so a run does not always pick the same way.
    const float hpFraction = player.hp / player.maxHp;

    if (profile_.cards == CardStyle::Greedy) {
        switch (card.kind) {
            case CardKind::Evolution: return 100.0f;
            case CardKind::Weapon: return (findWeapon(player, card.weapon) ? 60.0f : 80.0f) + jitter;
            case CardKind::Passive: return 50.0f + jitter;
            case CardKind::Heal: return hpFraction < 0.3f ? 70.0f : 10.0f;
        }
        return 0.0f;
    }

    // Planner: the Skilled player's reasoning.
    const balance::Evolution* recipe = targetRecipe(state);
    switch (card.kind) {
        case CardKind::Evolution: return 100.0f;
        case CardKind::Weapon: {
            const WeaponSlot* held = findWeapon(player, card.weapon);
            if (!held) {
                // Early on, more weapons mean more firepower, so new weapons are grabbed first.
                return (player.weaponCount < 4 ? 66.0f : (player.weaponCount < 5 ? 50.0f : 35.0f)) + jitter;
            }
            if (static_cast<int>(card.weapon) == favorite_) return 80.0f + jitter;
            if (recipe && recipe->weapon == card.weapon) return 70.0f + jitter;
            // An upgrade also refills that gun, which matters when it is low.
            const balance::WeaponLevel stats = weapons::effectiveStats(state, *held);
            const bool lowAmmo = stats.magazine > 0 && held->magazine + held->reserve < stats.maxReserve / 3;
            return (lowAmmo ? 62.0f : 45.0f) + jitter;
        }
        case CardKind::Passive: {
            if (recipe && recipe->passive == card.passive) {
                const WeaponSlot* weapon = findWeapon(player, recipe->weapon);
                // The passive only matters once its weapon is close to level 5.
                return (weapon && weapon->level >= 3 ? 66.0f : 44.0f) + jitter;
            }
            switch (card.passive) {
                case PassiveId::Damage: return 46.0f + jitter;
                case PassiveId::FireRate: return 42.0f + jitter;
                case PassiveId::Armor: return 41.0f + jitter;
                case PassiveId::Area: return 38.0f + jitter;
                case PassiveId::MoveSpeed: return 36.0f + jitter;
                default: return 30.0f + jitter;
            }
        }
        case CardKind::Heal: return hpFraction < 0.4f ? 75.0f : 15.0f;
    }
    return 0.0f;
}

void Bot::decideCard(const GameState& state, InputFrame& input) {
    const RunState& run = state.run;
    if (run.offerCount <= 0) return;
    if (profile_.cards == CardStyle::Random) {
        input.chooseCard = static_cast<int8_t>(rng_.below(run.offerCount));
        return;
    }
    int best = 0;
    float bestScore = -1.0f;
    for (int i = 0; i < run.offerCount; ++i) {
        const float score = scoreCard(state, run.offers[i]);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    // The Planner spends a reroll when nothing on offer is worth having.
    constexpr float kUsefulScore = 44.0f;
    if (profile_.cards == CardStyle::Planner && bestScore < kUsefulScore && run.rerollsLeft > 0) {
        input.reroll = true;
        return;
    }
    input.chooseCard = static_cast<int8_t>(best);
}

}  // namespace ls::tools
