#pragma once

#include "core/GameState.h"

namespace ls::worldview {

// Builds the sprite atlas, shaders and ground pool. Call once after the window opens.
void init();

// Draws the whole world into a low-resolution picture, then scales it up over the
// screen. Call inside BeginDrawing, before any interface drawing, which then lands on
// top at full resolution. alpha is how far the renderer is between the previous
// simulation step and the latest one.
void draw(const GameState& state, float alpha);

// Reads this frame's simulation events, for things like muzzle flash timing. What it
// starts belongs to the renderer, so the simulation is never changed.
void consumeEvents(const GameState& state);

// Fast mode draws fewer full-detail figures, for weak devices.
void setFastMode(bool enabled);

// 0 is for weak devices: half-resolution ground, with no lamps or flicker. 1 is normal.
void setQuality(int quality);

// Holds the day cycle at a fixed time, for screens like the title demo. A negative
// value goes back to following the run. theme::kDuskSeconds is the title screen's mood.
void setFixedDayTime(float seconds);

// Debug only: fills the pools with one of every world object, so every shape can be
// seen. extraZombies adds a mixed crowd, for checking the cheap figures. A negative
// value leaves the world empty and stops the clock, for checking the ground.
void setupShowcase(GameState& state, int extraZombies);

// Debug only: keeps the showcase animating while the simulation is held still.
void tickShowcase(GameState& state);

}  // namespace ls::worldview
