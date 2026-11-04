#pragma once

#include "core/GameState.h"

// The player's held weapons and passives as a row of icon slots. Shared by the HUD,
// the level-up screen and the pause menu, so a build always looks the same.
namespace ls::ui {

// Space between slots, as a share of the slot size.
inline constexpr float kSlotGap = 0.12f;

// Draws held weapons starting at x, y. Each has a level badge and, for guns, a
// magazine bar under it with reload progress and an EMPTY tag when dry.
// Returns the width used.
float drawWeaponSlots(const GameState& state, float x, float y, float slot);

// The height of a weapon row, including the magazine bar.
float weaponSlotsHeight(float slot);

// Draws held passives with their level. Returns the width used.
float drawPassiveSlots(const GameState& state, float x, float y, float slot);

}  // namespace ls::ui
