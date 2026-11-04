#include "core/App.h"

#include "components/Input/Input.h"
#include "components/Ui/Hud/Hud.h"
#include "components/Ui/LevelUpCards/LevelUpCards.h"
#include "components/WorldView/WorldView.h"
#include "core/Platform.h"
#include "core/Sim.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace ls::app {

namespace {

enum DebugCommand { kStressEnemies = 1, kStressProjectiles = 2, kSetSeed = 3, kGrantXp = 4, kShowcase = 5 };

constexpr int kMaxStepsPerFrame = 5;  // Stops a slow frame causing an even slower one.

GameState* g_state = nullptr;
input::Snapshot g_input;
float g_accumulator = 0.0f;
bool g_debug = false;
bool g_showcase = false;  // Debug only: holds the simulation still, so every shape can be seen.
uint64_t g_seedOverride = 0;

// Rolling averages, for the debug overlay and performance tests.
float g_frameMs = 16.6f;
float g_simMs = 0.0f;
float g_drawMs = 0.0f;
int g_lastSteps = 0;

// A menu choice waits here until a simulation step runs. On a fast display many
// frames run no step at all, so a choice tied to its own frame could be lost.
ui::LevelUpChoice g_pendingChoice;

float smooth(float average, float sample) { return average + (sample - average) * 0.05f; }

uint64_t pickSeed() {
    if (g_seedOverride != 0) return g_seedOverride;
    return static_cast<uint64_t>(std::time(nullptr)) ^ (static_cast<uint64_t>(GetRandomValue(0, 0x7fffffff)) << 20);
}

void runSimulation(float dtReal) {
    const double start = GetTime();
    g_accumulator += dtReal;
    InputFrame frameInput;
    frameInput.moveX = g_input.moveX;
    frameInput.moveY = g_input.moveY;

    int steps = 0;
    while (g_accumulator >= kDt && steps < kMaxStepsPerFrame) {
        // A menu choice applies once, even when this frame runs several steps.
        frameInput.chooseCard = static_cast<int8_t>(g_pendingChoice.card);
        frameInput.reroll = g_pendingChoice.reroll;
        g_pendingChoice = ui::LevelUpChoice{};
        sim::step(*g_state, frameInput);
        g_accumulator -= kDt;
        steps++;
    }
    if (steps == kMaxStepsPerFrame) g_accumulator = 0.0f;
    g_lastSteps = steps;
    // Each step's time, not each frame's, so numbers compare across refresh rates.
    if (steps > 0) g_simMs = smooth(g_simMs, static_cast<float>((GetTime() - start) * 1000.0) / static_cast<float>(steps));
}

void drawDebugOverlay() {
    char text[160];
    std::snprintf(text, sizeof(text), "%d fps  frame %.1f ms  sim %.2f ms  draw %.2f ms\nenemies %d  shots %d  bullets %d  gems %d  events %d",
                  GetFPS(), static_cast<double>(g_frameMs), static_cast<double>(g_simMs), static_cast<double>(g_drawMs),
                  g_state->enemies.count, g_state->projectiles.count, g_state->enemyBullets.count, g_state->gems.count, g_state->events.count);
    DrawText(text, 12, GetScreenHeight() - 60, 20, theme::kDebugText);
}

}  // namespace

void init() {
    // This is a 2D game, and shapes are not all wound the same way.
    rlDisableBackfaceCulling();
    g_state = new GameState();
    g_debug = platform::debugEnabled();
    sim::newRun(*g_state, pickSeed());
}

void frame() {
    platform::syncCanvasSize(1.0f);
    const float rawDt = GetFrameTime();
    g_frameMs = smooth(g_frameMs, rawDt * 1000.0f);
    const float dtReal = std::min(rawDt, 0.1f);

    input::update(g_input);

    if (g_state->run.phase == RunPhase::LevelUp) {
        const ui::LevelUpChoice choice = ui::updateLevelUpCards(*g_state, g_input, dtReal);
        if (choice.card >= 0) g_pendingChoice.card = choice.card;
        if (choice.reroll) g_pendingChoice.reroll = true;
    } else if ((g_state->run.phase == RunPhase::Dead || g_state->run.phase == RunPhase::Won) &&
               (g_input.confirm || g_input.pointerPressed)) {
        sim::newRun(*g_state, pickSeed());
    }

    if (g_showcase) worldview::tickShowcase(*g_state);
    else runSimulation(dtReal);
    worldview::consumeEvents(*g_state);
    g_state->events.clear();

    const double drawStart = GetTime();
    const float alpha = g_accumulator / kDt;
    BeginDrawing();
    ClearBackground(theme::kBackground);
    const Camera2D camera = worldview::makeCamera(*g_state, alpha);
    worldview::draw(*g_state, alpha, camera);
    ui::drawHud(*g_state);
    if (g_state->run.phase == RunPhase::LevelUp) ui::drawLevelUpCards(*g_state);
    if (g_state->run.phase == RunPhase::Dead || g_state->run.phase == RunPhase::Won) {
        const char* message = g_state->run.phase == RunPhase::Dead ? "YOU DIED - PRESS TO RESTART" : "VICTORY - PRESS TO RESTART";
        const int size = std::max(20, GetScreenWidth() / 28);
        DrawText(message, (GetScreenWidth() - MeasureText(message, size)) / 2, GetScreenHeight() / 2, size, theme::kText);
    }
    if (g_debug) drawDebugOverlay();
    g_drawMs = smooth(g_drawMs, static_cast<float>((GetTime() - drawStart) * 1000.0));
    EndDrawing();
}

const char* queryState() {
    static char json[512];
    const GameState& s = *g_state;
    std::snprintf(json, sizeof(json),
                  "{\"phase\":%d,\"tick\":%u,\"kills\":%d,\"level\":%d,\"hp\":%.1f,\"x\":%.1f,\"y\":%.1f,"
                  "\"enemies\":%d,\"shots\":%d,\"bullets\":%d,\"gems\":%d,\"offers\":%d,\"rerolls\":%d,"
                  "\"fps\":%d,\"frameMs\":%.2f,\"simMs\":%.3f,\"drawMs\":%.3f,\"width\":%d,\"height\":%d,\"hash\":\"%llx\"}",
                  static_cast<int>(s.run.phase), s.run.tick, s.run.kills, s.player.level, static_cast<double>(s.player.hp),
                  static_cast<double>(s.player.pos.x), static_cast<double>(s.player.pos.y), s.enemies.count, s.projectiles.count,
                  s.enemyBullets.count, s.gems.count, s.run.offerCount, s.run.rerollsLeft, GetFPS(),
                  static_cast<double>(g_frameMs), static_cast<double>(g_simMs), static_cast<double>(g_drawMs),
                  GetScreenWidth(), GetScreenHeight(), static_cast<unsigned long long>(sim::stateHash(s)));
    return json;
}

void debugCommand(int command, int value) {
    if (!g_debug) return;
    // Any other debug command hands control back to the simulation.
    g_showcase = command == kShowcase;
    switch (command) {
        case kStressEnemies: sim::setStressTargets(*g_state, value, g_state->run.debugProjectileTarget); break;
        case kStressProjectiles: sim::setStressTargets(*g_state, g_state->run.debugEnemyTarget, value); break;
        case kShowcase: worldview::setupShowcase(*g_state, value); break;
        case kSetSeed:
            g_seedOverride = static_cast<uint64_t>(value);
            sim::newRun(*g_state, g_seedOverride);
            break;
        case kGrantXp: g_state->player.xp = std::max(0, g_state->player.xpToNext - value); break;
        default: break;
    }
}

}  // namespace ls::app
