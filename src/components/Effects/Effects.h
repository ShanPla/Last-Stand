#pragma once

#include "components/WorldView/Projection.h"
#include "core/GameState.h"

#include "raylib.h"

// Game feel: particles, damage numbers, screen shake, hit pause, glow and flashes.
// This only ever reads simulation state and events, and it has its own random number
// generator, so nothing here can change how a run plays out. All its memory is fixed
// when the program starts.
namespace ls::effects {

using worldview::View;
using worldview::ScreenPoint;
using worldview::project;
using worldview::onTarget;

// The player's settings, passed in whenever they change, so they apply at once.
struct Settings {
    float screenShake = 1.0f;     // 0 to 1. Scales all screen shake.
    float flashIntensity = 1.0f;  // 0 to 1. Scales hit, screen and explosion flashes.
    bool damageNumbers = true;
    bool reducedMotion = false;   // No shake or hit pause, fewer particles, no flicker.
};

// Compiles the glow shader and bakes the digits. Call once after the window opens.
void init();

void applySettings(const Settings& settings);

// 0 is for weak devices: no glow, no ground stains and half the particles. 1 is normal.
void setQuality(int quality);
int quality();

// Empties everything, for a new run or the title screen.
void reset();

// --- Once a frame, from the app loop ---

// Reads this frame's events. quiet is for the title screen demo, which shows particles
// but no shake, flashes, hit pause or numbers.
void consumeEvents(const GameState& state, bool quiet);

// Moves everything on. worldMoving is false while a menu or a hit pause holds the world.
void update(float dtReal, bool worldMoving);

// The hit pause: takes the frame's real time and returns what the simulation may have.
float filterSimulationTime(float dtReal);
bool hitPauseActive();

// --- For the world renderer ---

// The camera's offset this frame, always in whole art pixels.
void shakeArtPixels(int* columns, int* rows);

// How many zombies are flashing this frame, set before any zombie is drawn.
void setFlashingCount(int flashing);
// How far a figure mixes toward the flash color, from 0 to 255. Zombies are thinned
// out in a crowd, while the soldier, elites and the boss always show their hits.
uint8_t zombieFlash(float secondsLeft, uint32_t id, bool important, bool boss);
uint8_t soldierFlash(float secondsLeft);

// 0 to 1, for anything else that counts as a flash, such as the muzzle flash.
float flashScale();
// How bright a blast's light is, given how much of its life is left.
float explosionLight(float life);
// False under reduced motion or with flashes turned off: blinking warnings stay steady.
bool flickerAllowed();
bool reducedMotion();
bool glowEnabled();

void queueGlowSpot(float x, float y, float radius, Color color, float strength);

// Called each frame the boss can really be seen, for its entrance.
void noteBossOnScreen(bool onScreen, const GameState& state);

// Flat, on the ground, beneath everything else. Goes inside an open flat pass.
void drawStains(const View& view, const float spriteTint[3]);
// Flat, above the crowd. Goes inside an open flat pass.
void drawParticles(const View& view, const float spriteTint[3]);
// These three set up their own drawing, and must be drawn before enemy bullets, so that
// nothing here can ever hide one.
void drawGlow(const View& view);
void drawScreenFlash(const View& view);
void drawNumbers(const View& view, const GameState& state);

// --- For the browser tests ---
const char* debugJson();
void resetDebugCounters();

}  // namespace ls::effects
