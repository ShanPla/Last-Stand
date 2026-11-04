#pragma once

#include "core/GameState.h"

// Generated sound effects and music. Everything is synthesized in code at startup,
// so the game ships with no audio files.
namespace ls::audio {

enum class UiSound { Move, Confirm, Back, CardPick, Reroll, Pause };

// Prepares sound data. Plays nothing until unlock() is called.
void init();

// Call on the first user input. Browsers block audio until then.
void unlock();

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
