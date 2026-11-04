#pragma once

#include "core/GameState.h"

#include "raylib.h"

namespace ls::worldview {

// Builds the camera that follows the player. alpha is how far the renderer is
// between the previous simulation step and the latest one.
Camera2D makeCamera(const GameState& state, float alpha);

// Draws the grid and every object in the world.
void draw(const GameState& state, float alpha, const Camera2D& camera);

}  // namespace ls::worldview
