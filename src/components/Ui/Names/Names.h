#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

// Player-facing names, and lookups for the evolution recipes.
namespace ls::ui {

const char* weaponName(WeaponId id);
const char* passiveName(PassiveId id);
// "Easy", "Normal" or "Hard". The same list feeds the title selector.
const char* difficultyName(Difficulty level);
const char* const* difficultyNames();

// The weapon name, or the evolution name once it has evolved.
const char* slotName(const WeaponSlot& slot);

// The recipe a weapon or passive belongs to, or null. Each weapon and each passive
// is part of at most one recipe.
const balance::Evolution* evolutionForWeapon(WeaponId id);
const balance::Evolution* evolutionForPassive(PassiveId id);

}  // namespace ls::ui
