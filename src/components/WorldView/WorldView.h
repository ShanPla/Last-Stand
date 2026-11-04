#pragma once

#include "core/GameState.h"

#include "raylib.h"

namespace ls::worldview {

// Builds the camera that follows the player. alpha is how far the renderer is
// between the previous simulation step and the latest one.
Camera2D makeCamera(const GameState& state, float alpha);

// Draws the grid and every object in the world.
void draw(const GameState& state, float alpha, const Camera2D& camera);

// Reads this frame's simulation events, for things like muzzle flash timing. The
// timers it starts belong to the renderer, so the simulation is never changed.
void consumeEvents(const GameState& state);

// Fast mode draws far fewer full-detail zombies, for weak devices.
void setFastMode(bool enabled);

// Debug only: fills the pools with one of every world object, so every shape can
// be seen. extraZombies adds a mixed crowd, for checking the cheap figures.
void setupShowcase(GameState& state, int extraZombies);

// Debug only: keeps the showcase animating while the simulation is held still.
void tickShowcase(GameState& state);

}  // namespace ls::worldview
