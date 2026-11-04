#pragma once

#include "components/WorldView/Draw.h"
#include "core/GameState.h"

// Everything in the world that isn't a character.
namespace ls::worldview {

// Flat on the ground, beneath characters: blasts, boss telegraphs and mines.
void drawGroundZones(const GameState& state, const ViewBounds& view);
void drawMines(const GameState& state, float alpha, const ViewBounds& view);

// Rewards lie beneath characters too.
void drawGems(const GameState& state, float alpha, const ViewBounds& view);
void drawPickups(const GameState& state, const ViewBounds& view);

// Above characters: the player's shots, beams and blades.
void drawBlades(const GameState& state, float alpha);
void drawPlayerShots(const GameState& state, float alpha, const ViewBounds& view);
void drawBeams(const GameState& state, float pixel);

// Drawn last of all, so nothing can ever hide an enemy bullet.
void drawEnemyBullets(const GameState& state, float alpha, const ViewBounds& view);

}  // namespace ls::worldview
