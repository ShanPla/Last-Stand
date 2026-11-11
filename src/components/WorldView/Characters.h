#pragma once

#include "components/WorldView/DayCycle.h"
#include "components/WorldView/Draw.h"
#include "core/GameState.h"

// Picks and places the sprites for zombies and the soldier. Positions come in already
// projected, so these functions only care about which frame to show and where.
namespace ls::worldview {

void drawZombieShadow(const GameState& state, const Enemy& enemy, const ScreenPoint& feet);
// A long sun shadow in the shape of the figure. Costs one more quad per figure.
void drawZombieLongShadow(const GameState& state, const Enemy& enemy, const ScreenPoint& feet, const DayLook& look);
void drawZombie(const GameState& state, const Enemy& enemy, const ScreenPoint& feet);
// A faint copy of the zombie, cropped to a rectangle of the picture, for showing through a car.
void drawZombieSilhouette(const GameState& state, const Enemy& enemy, const ScreenPoint& feet, uint8_t alpha, float left, float top, float right, float bottom);

// The small HP bar over an elite. A flat shape, drawn above the crowd.
void drawEliteBar(const GameState& state, const Enemy& enemy, const ScreenPoint& feet);

// True while a zombie is warning of an attack: the Charger's line, the boss's ring and line.
bool isTelegraphing(const Enemy& enemy);
// Draws that warning as flat shapes above the crowd. It needs no figure on screen, because
// the warning can cross the picture while the zombie stands outside it. Returns whether
// any of it could reach the picture.
bool drawTelegraph(const View& view, const GameState& state, const Enemy& enemy, Vec2 worldFeet);

// How far a figure reaches from its feet, in art pixels at scale 1, for skipping
// figures that are off screen.
float figureHeight(const Enemy& enemy);
float figureHalfWidth(const Enemy& enemy);
// How far right of its feet a figure's long sun shadow reaches, at scale 1. A figure past
// the left edge can still throw this much shadow into the picture.
float longShadowReach(const Enemy& enemy, float shadowLength);
// True when any part of the figure's real sprite is inside the picture. figureHeight
// and figureHalfWidth are deliberately generous, so they can't answer this.
bool figureTouchesTarget(const View& view, const GameState& state, const Enemy& enemy, const ScreenPoint& feet);

// Soldiers. slot is the player's slot in state.players. In single-player it is always 0.
void drawSoldierShadow(const GameState& state, int slot, const ScreenPoint& feet, const DayLook& look);
void drawSoldier(const GameState& state, int slot, const ScreenPoint& feet, uint8_t strength);
void drawSoldierRing(const View& view, Vec2 worldFeet);
void drawSoldierRingOverCrowd(const View& view, Vec2 worldFeet);
// The ring under another co-op player, in that player's color.
void drawTeammateRing(const View& view, Vec2 worldFeet, int slot);
void drawMuzzleFlash(const GameState& state, int slot, const ScreenPoint& feet);
// The pale afterimage behind this screen's own soldier during a dash. Goes inside an open
// sprite pass, under the figures.
void drawDashTrail(const View& view, const GameState& state, Vec2 playerWorld);

// Updates a soldier's walk cycle and timers, once per drawn frame. These live here,
// not in the simulation, because they only change how things look.
void updateSoldierAnimation(int slot, Vec2 feetWorld, float realDt);
void noteWeaponFired(int slot, WeaponId weapon);
// Forgets the last gun and every timer, so a new run doesn't start with the old run's gun.
void resetSoldierAnimation();
// Co-op: the color each player slot wears, as an index into theme::kCoopSlotColors.
void setSoldierColors(const int colors[kMaxPlayers]);

// For the browser tests: what the soldier looks like right now, and how many frames have
// shown a muzzle flash.
struct SoldierDebug {
    int heldGun;
    int muzzleFrames;
};
SoldierDebug soldierDebug(const GameState& state);

// Fallen zombies live in a small pool owned by the renderer.
void addCorpse(EnemyType type, Vec2 pos, uint32_t tick);
void drawCorpses(const View& view, uint32_t tick);

}  // namespace ls::worldview
