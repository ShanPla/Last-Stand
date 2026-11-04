#pragma once

#include "components/WorldView/Draw.h"
#include "core/GameState.h"

// Draws zombies and the soldier. Positions come in already interpolated and
// projected, so these functions only care about how each figure looks.
namespace ls::worldview {

void drawZombieShadow(const Enemy& enemy, Vec2 feet, bool detailed);
void drawZombie(const GameState& state, const Enemy& enemy, Vec2 feet, bool detailed);

// Things that must stay readable, so they are drawn above the crowd: the
// Charger's warning line, and the HP bar over elites.
void drawZombieOverlay(const GameState& state, const Enemy& enemy, Vec2 feet);

// True for a zombie that must keep its full figure at any distance.
bool mustStayDetailed(const Enemy& enemy);

// Roughly how far a zombie's figure reaches, for skipping off-screen ones.
float figureHeight(const Enemy& enemy);
float figureHalfWidth(const Enemy& enemy);

void drawSoldierShadow(Vec2 feet);
void drawSoldier(const GameState& state, Vec2 feet);
void drawMuzzleFlash(const GameState& state, Vec2 feet);

// Updates the soldier's walk cycle and timers, once per drawn frame. These live
// here, not in the simulation, because they only change how things look.
void updateSoldierAnimation(const GameState& state, Vec2 feetWorld, float realDt);

// Notes which gun fired, for the muzzle flash and the gun in the soldier's hands.
void noteWeaponFired(WeaponId weapon);

}  // namespace ls::worldview
