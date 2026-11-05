#pragma once

#include "core/GameState.h"

// Generated sound effects and music. Everything is synthesized in code at startup,
// so the game ships with no audio files.
namespace ls::audio {

enum class UiSound { Move, Confirm, Back, CardPick, Reroll, Pause };

// Prepares sound data. Plays nothing until unlock() is called.
void init();

// Opens the audio device and makes sure sound can be heard. Browsers block audio until
// the player has pressed something, and some presses (Esc, a gamepad button) do not count.
// So call this every frame until it returns true, with pressed set on frames where a key,
// button or tap went down. It only asks the browser to start sound on those frames.
// Returns true once sound is really running.
bool unlock(bool pressed);

// For tests: whether sound is running, and the music's current duck gain (1 is not ducked).
bool isRunning();
float duckLevel();

// Call once per frame. intensity is 0 to 1, and boss is true during the boss fight.
void update(float dtReal, float intensity, bool boss);

// Call for every simulation event, before the event list is cleared.
void onEvent(const SimEvent& event);

void playUi(UiSound sound);

// Both are 0 to 1, and apply immediately.
void setMusicVolume(float volume);
void setEffectsVolume(float volume);

// Fades music down while paused or in a menu, without stopping it.
void setDucked(bool ducked);

}  // namespace ls::audio
