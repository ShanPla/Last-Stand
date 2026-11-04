#pragma once

#include "components/WorldView/Draw.h"
#include "core/GameState.h"

// Everything in the world that isn't a character or the ground.
namespace ls::worldview {

// Flat on the ground, beneath characters: boss telegraphs and mine trigger rings.
void drawGroundMarks(const View& view, const GameState& state, float alpha);

// Sprites beneath characters: mines, gems and pickups, with their shadows.
void drawGroundSprites(const View& view, const GameState& state, float alpha);

// Flat, above characters: bullets, beams, lightning and the rings of blasts.
void drawShots(const View& view, const GameState& state, float alpha);
void drawBlasts(const View& view, const GameState& state);
void drawBeams(const View& view, const GameState& state);
void drawBladeTrails(const View& view, const GameState& state, float alpha);

// The same things again, as a soft bright layer with additive blending.
void drawGlows(const View& view, const GameState& state);

// Sprites above characters: blast fire, flames, grenades, rockets and blades.
void drawAirSprites(const View& view, const GameState& state, float alpha);

// Drawn last of all, so nothing can ever hide an enemy bullet.
void drawEnemyBullets(const View& view, const GameState& state, float alpha);

// Smoke puffs live in a small pool owned by the renderer.
void updateSmoke(const GameState& state);
void drawSmoke(const View& view, uint32_t tick);

}  // namespace ls::worldview
