#pragma once

#include "core/GameState.h"
#include "core/Rng.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <vector>

namespace ls::tools {

// How a bot picks level-up cards.
enum class CardStyle {
    Random,   // Any card, like a player who does not read them.
    Greedy,   // New weapons first, then upgrades, then passives.
    Planner,  // Builds toward one evolution recipe, and rerolls weak offers.
};

// The knobs that make one bot weaker or stronger than another. Each profile is a
// deliberate approximation of a kind of human player, not a measurement of one.
struct BotProfile {
    const char* name;
    // Reaction: the bot only re-decides its movement every so many steps.
    int reactMinSteps;
    int reactMaxSteps;
    // Dodging.
    float lookRadius;          // Zombies further than this are not noticed.
    int directions;            // Headings it compares each time it decides.
    float lookAheadSeconds;    // How far ahead it imagines its own move.
    bool predictsMovement;     // Imagines zombies walking toward it, not standing still.
    bool avoidsCrowds;         // Also steers away from thick crowds further out.
    float bulletSight;         // Enemy bullets closer than this are noticed. Zero means it ignores them.
    float telegraphChance;     // Chance per decision that it notices boss and charger warnings.
    // Mistakes.
    float noiseRadians;        // Random error added to the chosen heading.
    float wanderChance;        // Chance per decision of walking a random way instead.
    // Collecting.
    float lootRadius;          // How far it will detour for gems and pickups.
    float lootWeight;          // How much it values loot against danger.
    bool knowsAmmoNeed;        // Heads for crates when guns run low.
    // Dashing. A dash is only spent on a real problem, and only when it would end somewhere safer.
    float dashChance;          // Chance per decision to dash once there is a reason. Zero never dashes.
    float dashAfterSeconds;    // Run time before it starts to dash at all.
    int dashCrowd;             // Zombies within touching range that make it feel surrounded.
    float dashHurtBelow;       // Being surrounded only counts as a reason once health is under this share.
    bool dashDodges;           // Also dashes out of boss and charger warnings and shots about to land.
    // Level-ups.
    CardStyle cards;
};

const BotProfile& novice();
const BotProfile& average();
const BotProfile& skilled();

// Plays the game by producing one InputFrame per step. It only reads the state.
// Its own random generator is separate from the simulation's, so a bot never
// changes how the game itself rolls dice.
class Bot {
public:
    // A favorite weapon (index, or -1) is the one the card choices build toward.
    Bot(const BotProfile& profile, uint64_t seed, int favoriteWeapon = -1);

    InputFrame act(const GameState& state);

private:
    // An area the player must not stand in when its timer ends. A circle is a
    // segment whose two ends are the same point.
    struct Hazard {
        Vec2 a;
        Vec2 b;
        float radius;
        float life;
        float weight;
    };
    struct Threat {
        Vec2 pos;
        Vec2 vel;
        float radius;
        float weight;
    };

    void decideMove(const GameState& state);
    void gatherThreats(const GameState& state);
    void considerDash(const GameState& state);
    float dashCost(Vec2 where, float seconds) const;
    void chooseLoot(const GameState& state);
    float headingCost(const GameState& state, Vec2 dir) const;
    float ammoNeed(const GameState& state) const;

    void decideCard(const GameState& state, InputFrame& input);
    float scoreCard(const GameState& state, const Card& card);
    const balance::Evolution* targetRecipe(const GameState& state) const;

    BotProfile profile_;
    Rng rng_;
    int favorite_;
    Vec2 heading_{1.0f, 0.0f};
    int stepsUntilDecision_ = 0;

    std::vector<Threat> threats_;
    std::vector<Threat> bullets_;
    std::vector<Hazard> hazards_;
    Vec2 lootTarget_;
    bool hasLoot_ = false;
    bool dashNext_ = false;  // Dash on the next step.
};

}  // namespace ls::tools
