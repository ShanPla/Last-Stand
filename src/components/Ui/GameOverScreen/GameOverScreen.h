#pragma once

#include "core/GameState.h"

namespace ls::ui {

// What the end of a run shows. The app fills it in once, when the run ends.
struct RunSummary {
    bool won = false;
    Difficulty difficulty = Difficulty::Normal;  // New-best marks compare within this level only.
    float timeSeconds = 0.0f;
    int kills = 0;
    int level = 1;
    float damageByWeapon[static_cast<int>(WeaponId::Count)] = {};
    bool newBestTime = false;
    bool newBestKills = false;
    bool newBestLevel = false;
};

enum class GameOverAction { None, Restart, Quit };

// Draws the game over or victory screen. age is seconds since the run ended. The
// buttons ignore presses for a short moment, so a player still moving does not
// restart by accident. Restart is selected first.
GameOverAction drawGameOverScreen(const RunSummary& summary, float age);

// Puts the selection back on Restart. Call when the screen opens.
void resetGameOverScreen();

}  // namespace ls::ui
