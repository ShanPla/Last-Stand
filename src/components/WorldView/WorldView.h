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

// Debug only: outlines each car's solid box over the picture, for lining the two up.
void setShowCarBoxes(bool show);
// Debug only: with this off, cars hide nothing, so tests can compare the two pictures.
void setCarCover(bool on);

// Debug only: fills the pools with one of every world object, so every shape can be
// seen. extraZombies adds a mixed crowd, for checking the cheap figures. A negative
// value leaves the world empty and stops the clock, for checking the ground. The values
// below each set up one small scene instead, for the browser tests.
inline constexpr int kSceneChargerOffLeft = -100;  // Off the left edge, aiming across the picture.
inline constexpr int kSceneChargerAway = -101;     // The same, aiming out of the picture.
inline constexpr int kSceneBossOffLeft = -102;     // A boss charging a dash from off the left edge.
inline constexpr int kSceneOnlyMines = -103;       // Landmines as the only weapon, and one target.
inline constexpr int kSceneWalkerOffLeft = -104;   // Just past the left edge, with only its shadow showing.
inline constexpr int kSceneCarEmpty = -105;        // A car, with the soldier beside it.
inline constexpr int kSceneCarBehind = -106;       // The same, with a zombie behind the car.
inline constexpr int kSceneCarFront = -107;        // The same, with a zombie in front of the car.
inline constexpr int kSceneCarBoth = -108;         // The same, with both.
inline constexpr int kSceneCarOfSprite = -120;     // The first car with sprite number N, at -120 - N, with the soldier beside it.
inline constexpr int kSceneCarOverEdge = -130;     // The first car whose picture crosses a chunk's edge.
void setupShowcase(GameState& state, int extraZombies);

// Debug only: flattens the car nearest the soldier, the way the boss's body would.
void crushNearestCar(GameState& state);

// Debug only: the first side-on car, with the boss about to walk over it toward the soldier
// and the simulation really running, for the moment of a crush.
void placeBossBesideCar(GameState& state);

// Debug only: keeps the showcase animating while the simulation is held still.
void tickShowcase(GameState& state);

}  // namespace ls::worldview
