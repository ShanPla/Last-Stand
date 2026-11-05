#pragma once

#include "components/WorldView/DayCycle.h"
#include "components/WorldView/Draw.h"
#include "core/GameState.h"

// Picks and places the sprites for zombies and the soldier. Positions come in already
// projected, so these functions only care about which frame to show and where.
namespace ls::worldview {

void drawZombieShadow(const Enemy& enemy, const ScreenPoint& feet);
// A long sun shadow in the shape of the figure. Costs one more quad per figure.
void drawZombieLongShadow(const GameState& state, const Enemy& enemy, const ScreenPoint& feet, const DayLook& look);
void drawZombie(const GameState& state, const Enemy& enemy, const ScreenPoint& feet);

// Flat overlays that must stay readable above the crowd: the Charger's warning line,
// and the HP bar over elites and the boss's charge ring.
void drawZombieOverlay(const View& view, const GameState& state, const Enemy& enemy, const ScreenPoint& feet, Vec2 worldFeet);
bool hasOverlay(const Enemy& enemy);

// How far a figure reaches from its feet, in art pixels at scale 1, for skipping
// figures that are off screen.
float figureHeight(const Enemy& enemy);
float figureHalfWidth(const Enemy& enemy);
// True when any part of the figure's real sprite is inside the picture. figureHeight
// and figureHalfWidth are deliberately generous, so they can't answer this.
bool figureTouchesTarget(const View& view, const GameState& state, const Enemy& enemy, const ScreenPoint& feet);

void drawSoldierShadow(const GameState& state, const ScreenPoint& feet, const DayLook& look);
void drawSoldier(const GameState& state, const ScreenPoint& feet, uint8_t strength);
void drawSoldierRing(const View& view, Vec2 worldFeet);
void drawSoldierRingOverCrowd(const View& view, Vec2 worldFeet);
void drawMuzzleFlash(const GameState& state, const ScreenPoint& feet);
// The pale afterimage behind him during a dash. Goes inside an open sprite pass, under the figures.
void drawDashTrail(const View& view, const GameState& state, Vec2 playerWorld);

// Updates the soldier's walk cycle and timers, once per drawn frame. These live here,
// not in the simulation, because they only change how things look.
void updateSoldierAnimation(Vec2 feetWorld, float realDt);
void noteWeaponFired(WeaponId weapon);

// Fallen zombies live in a small pool owned by the renderer.
void addCorpse(EnemyType type, Vec2 pos, uint32_t tick);
void drawCorpses(const View& view, uint32_t tick);

}  // namespace ls::worldview
