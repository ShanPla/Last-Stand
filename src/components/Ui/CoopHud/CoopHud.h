#pragma once

#include "core/GameState.h"

// What co-op adds over the HUD during a run: a name tag over each other player, a DOWN mark
// and revive bar over anyone who is down, a small connection mark, short notices (someone
// left, the host is away), and the network debug overlay. It only draws what it is given, so
// it knows nothing about the network itself.
namespace ls::ui {

// Kept for the whole run, so a player who leaves still has a name in the notice.
void coopHudBeginRun(const char* const names[kMaxPlayers], const int colors[kMaxPlayers]);
const char* coopHudName(int slot);

// A short message near the top of the screen, such as "Ana left the run". Shown for a few seconds.
void coopHudNotice(const char* text);
const char* coopHudLastNotice();
// Drops the notice on screen, so an old one never shows over a new run.
void coopHudClearNotice();

// Draws only the notice, for a run that is no longer co-op: when the host's Play Again finds
// nobody else connected, the solo run that follows still says why.
void drawCoopNotice(float dtReal);

struct CoopHudView {
    bool reconnecting = false; // This guest's connection dropped and it is getting its seat back.
    bool hostAway = false;     // The host's tab is hidden, or nothing has come from the host for a while.
    bool hostPaused = false;   // The host has the pause menu open.
    int pingMs = -1;           // Round trip to the host, or the slowest player for the host. -1 when unknown.
    float snapshotAge = 0.0f;  // Seconds since the last snapshot. Always 0 on the host.
    bool connected[kMaxPlayers] = {true, true, true, true};
    const char* debugText = nullptr;  // The overlay's lines, or null when it is hidden.
};

// Draws over the HUD. Call while a co-op run is on screen.
void drawCoopHud(const GameState& state, const CoopHudView& view, float dtReal);

// 0 (no signal) to 3 (good), from the round trip and how fresh the snapshots are.
int connectionQuality(int pingMs, float snapshotAge);

}  // namespace ls::ui
