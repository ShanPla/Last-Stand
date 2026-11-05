#include "core/App.h"

#include "components/Audio/Audio.h"
#include "components/Effects/Effects.h"
#include "components/Input/Input.h"
#include "components/Maps/Maps.h"
#include "components/Ui/Font/Font.h"
#include "components/Ui/GameOverScreen/GameOverScreen.h"
#include "components/Ui/Hud/Hud.h"
#include "components/Ui/HowToPlay/HowToPlay.h"
#include "components/Ui/LevelUpCards/LevelUpCards.h"
#include "components/Ui/PauseMenu/PauseMenu.h"
#include "components/Ui/SettingsScreen/SettingsScreen.h"
#include "components/Ui/TitleScreen/TitleScreen.h"
#include "components/Ui/VirtualJoystick/VirtualJoystick.h"
#include "components/Ui/Widgets/Widgets.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "components/WorldView/WorldView.h"
#include "core/Platform.h"
#include "core/Save.h"
#include "core/Sim.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

namespace ls::app {

namespace {

enum DebugCommand {
    kStressEnemies = 1,
    kStressProjectiles = 2,
    kSetSeed = 3,
    kGrantXp = 4,
    kStartRun = 5,       // Starts a run now. The value is a seed, or 0 for a random one.
    kForceDeath = 6,
    kForceVictory = 7,
    kShowScreen = 8,     // 0 title, 1 pause, 2 settings, 3 how to play.
    kSetSafeArea = 9,    // CSS pixels on every edge, or a negative value to clear.
    kGiveBuild = 10,     // A full build of weapons and passives. Value 1 also empties the first gun.
    kSetHp = 11,         // Percent of maximum health.
    kSetTime = 12,       // Seconds into the run.
    kSetKills = 13,
    kSetLevel = 14,
    kFakeDamage = 15,    // Fills the damage list, for the game over screenshot.
    kLevelUp = 16,       // Gives exactly the XP needed for the next level, so cards open at once.
    kShowcase = 17,  // Places one of every world object, for visual checks.
    kMovePlayerX = 18,
    kMovePlayerY = 19,
    kSetQuality = 20,
    kSetShake = 21,      // Percent. These four change a setting exactly as the screen does.
    kSetFlash = 22,      // Percent.
    kSetDamageNumbers = 23,
    kSetReducedMotion = 24,
    kPushEvent = 25,     // Sends an event of this type through the normal list, at the player.
    kResetFxCounters = 26,
    kRunSteps = 27,      // Runs this many steps at once with no input, then reads their events.
    kSetMap = 28,        // For tests only. A bad value falls back to the default map.
    kDash = 40,          // Presses dash, as the key would.
    kHoldSim = 41,       // 1 holds the simulation still (the screen still draws), 0 lets it run. For screenshots.
    kSetDashCooldown = 42,  // Percent of the full cooldown left.
};

// Which screen is showing. Level-up is not here: it is a phase of a running game.
enum class Screen { Title, Playing, Paused, Settings, HowToPlay, Over };

constexpr int kMaxStepsPerFrame = 5;  // Stops a slow frame causing an even slower one.
constexpr float kScreenFadeSeconds = 0.20f;  // Under the 300 ms limit, and input never waits for it.
constexpr float kRunStartFadeSeconds = 0.30f;
constexpr uint64_t kDemoSeed = 20240607;
constexpr int kDemoEnemies = 36;

GameState* g_state = nullptr;
input::Snapshot g_input;
float g_accumulator = 0.0f;
bool g_debug = false;
uint64_t g_seedOverride = 0;

Screen g_screen = Screen::Title;
Screen g_settingsFrom = Screen::Title;
bool g_inDemo = true;       // The state holds the title screen demo, not a real run.
float g_screenAge = 0.0f;   // Seconds on the current screen.
float g_phaseAge = 0.0f;    // Seconds in the current run phase, for the level-up fade.
RunPhase g_lastPhase = RunPhase::Playing;
float g_runStartFade = 0.0f;
float g_moveTime = 0.0f;    // Seconds the player has moved this run, which fades the controls hint.
float g_demoTime = 0.0f;
float g_demoAmmoTimer = 0.0f;
bool g_audioUnlocked = false;
bool g_settingsDirty = false;
bool g_ducked = true;
ui::RunSummary g_summary;

// Rolling averages, for the debug overlay and performance tests.
float g_frameMs = 16.6f;
float g_simMs = 0.0f;
float g_drawMs = 0.0f;
int g_lastSteps = 0;

// A menu choice waits here until a simulation step runs. On a fast display many
// frames run no step at all, so a choice tied to its own frame could be lost.
ui::LevelUpChoice g_pendingChoice;

// A dash press waits here the same way. It lasts until a step uses it, but never past
// the play screen: a press made on a menu is dropped, so Space that confirms a menu
// can't dash a moment later.
bool g_pendingDash = false;
bool g_holdSim = false;  // Debug only: the simulation stands still, for screenshots.

float smooth(float average, float sample) { return average + (sample - average) * 0.05f; }
float smoothstep01(float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

uint64_t pickSeed() {
    if (g_seedOverride != 0) return g_seedOverride;
    return static_cast<uint64_t>(std::time(nullptr)) ^ (static_cast<uint64_t>(GetRandomValue(0, 0x7fffffff)) << 20);
}

// ---- Saving ---------------------------------------------------------------------

void persist() {
    platform::writeSave(save::serialize(save::current()));
    g_settingsDirty = false;
}

// Called whenever a setting changes, so every change applies at once, even mid-fight.
void applySettings() {
    const Settings& settings = save::current().settings;
    audio::setMusicVolume(settings.musicVolume);
    audio::setEffectsVolume(settings.effectsVolume);
    effects::applySettings({settings.screenShake, settings.flashIntensity, settings.damageNumbers, settings.reducedMotion});
}

// A missing or broken save gives defaults. The system's reduced motion preference
// only matters when nothing was saved yet.
void loadSave() {
    SaveData defaults;
    defaults.settings.reducedMotion = platform::prefersReducedMotion();
    std::string text;
    if (platform::readSave(&text)) save::current() = save::parse(text, defaults);
    else save::current() = defaults;
    applySettings();
}

// ---- Screens and runs -----------------------------------------------------------

void setScreen(Screen screen) {
    g_screen = screen;
    g_screenAge = 0.0f;
    ui::consumePresses();
}

// The title screen runs the real simulation as a demo: fixed seed, a bot that walks
// in circles, enemies kept topped up, and no way to be hurt. It is never saved and
// never makes sound, because only a real run sends events to audio.
void startDemo() {
    worldview::setFixedDayTime(theme::kDuskSeconds);
    effects::reset();
    sim::newRun(*g_state, kDemoSeed);
    for (WeaponId id : {WeaponId::Shotgun, WeaponId::Smg, WeaponId::Flamethrower, WeaponId::Tesla, WeaponId::Blades}) {
        weapons::grant(*g_state, id);
    }
    sim::setStressTargets(*g_state, kDemoEnemies, 0);
    g_inDemo = true;
    g_demoTime = 0.0f;
    g_demoAmmoTimer = 0.0f;
    g_accumulator = 0.0f;
    g_pendingChoice = ui::LevelUpChoice{};
    ui::resetTitleScreen();
    setScreen(Screen::Title);
}

void startRun(uint64_t seed) {
    worldview::setFixedDayTime(-1.0f);
    effects::reset();
    sim::newRun(*g_state, seed != 0 ? seed : pickSeed());
    g_inDemo = false;
    g_accumulator = 0.0f;
    g_pendingChoice = ui::LevelUpChoice{};
    g_moveTime = 0.0f;
    g_runStartFade = 1.0f;
    g_lastPhase = RunPhase::Playing;
    g_phaseAge = 0.0f;
    ui::resetLevelUpCards();
    g_pendingDash = false;
    g_holdSim = false;
    setScreen(Screen::Playing);
}

void pauseGame() {
    if (g_inDemo || g_screen != Screen::Playing || g_state->run.phase != RunPhase::Playing) return;
    ui::resetPauseMenu();
    audio::playUi(audio::UiSound::Pause);
    setScreen(Screen::Paused);
}

void openSettings(Screen from) {
    g_settingsFrom = from;
    ui::resetSettingsScreen();
    setScreen(Screen::Settings);
}

void leaveSettings() {
    if (g_settingsDirty) persist();
    audio::playUi(audio::UiSound::Back);
    if (g_settingsFrom == Screen::Paused) ui::resetPauseMenu();
    if (g_settingsFrom == Screen::Title) ui::resetTitleScreen();
    setScreen(g_settingsFrom);
}

// Records a finished run: shows the summary, updates the best run and saves it.
void finishRun() {
    const GameState& s = *g_state;
    g_summary = ui::RunSummary{};
    g_summary.won = s.run.phase == RunPhase::Won;
    g_summary.timeSeconds = s.timeSeconds();
    g_summary.kills = s.run.kills;
    g_summary.level = s.player.level;
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) g_summary.damageByWeapon[i] = s.run.damageByWeapon[i];

    BestRun& best = save::current().best;
    g_summary.newBestTime = g_summary.timeSeconds > best.bestTimeSeconds;
    g_summary.newBestKills = g_summary.kills > best.mostKills;
    g_summary.newBestLevel = g_summary.level > best.highestLevel;
    best.bestTimeSeconds = std::max(best.bestTimeSeconds, g_summary.timeSeconds);
    best.mostKills = std::max(best.mostKills, g_summary.kills);
    best.highestLevel = std::max(best.highestLevel, g_summary.level);
    if (g_summary.won) best.wins++;
    best.runs++;
    persist();

    ui::resetGameOverScreen();
    setScreen(Screen::Over);
}

// ---- Simulation -----------------------------------------------------------------

bool g_showcase = false;  // Debug only: holds the simulation still, so every shape can be seen.

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
        // One press is one dash: the first step of the frame takes it, the others don't.
        frameInput.dash = g_pendingDash;
        g_pendingDash = false;
    if (steps > 0) g_simMs = smooth(g_simMs, static_cast<float>((GetTime() - start) * 1000.0) / static_cast<float>(steps));
}

// The scripted bot for the title screen: walks a wandering loop and never takes damage.
void runDemo(float dtReal) {
    g_accumulator += dtReal;
    g_demoAmmoTimer += dtReal;
    if (g_demoAmmoTimer > 4.0f) {
        g_demoAmmoTimer = 0.0f;
        weapons::addAmmoFraction(*g_state, 1.0f);  // Keeps the guns firing.
    }
    int steps = 0;
    while (g_accumulator >= kDt && steps < kMaxStepsPerFrame) {
        g_demoTime += kDt;
        InputFrame bot;
        bot.moveX = std::cos(g_demoTime * 0.55f) * 0.9f;
        bot.moveY = std::sin(g_demoTime * 0.37f) * 0.9f;
        sim::step(*g_state, bot);
        // The clock loops, so the demo never grows into a late-game crowd.
        if (g_state->run.tick > 1800) g_state->run.tick = 300;
        if (g_state->run.phase != RunPhase::Playing) {
            startDemo();
            break;
        }
        g_accumulator -= kDt;
        steps++;
    }
    if (steps == kMaxStepsPerFrame) g_accumulator = 0.0f;
}

// ---- Weak devices ---------------------------------------------------------------

// A phone that can't hold a steady frame rate loses the extras: the glow, the stains and
// half the particles. It only ever steps down, as bouncing between looks would be worse.
// Only frames that ran a fight count, so a slow first second of loading can't trigger it.
constexpr float kSlowFrameMs = 26.0f;       // Worse than about 38 frames a second.
constexpr float kSlowForSeconds = 4.0f;
bool g_qualityPinned = false;               // Debug only: a test chose the quality itself.
float g_slowSeconds = 0.0f;

void watchFrameRate(float rawDt) {
    if (g_qualityPinned || effects::quality() == 0 || g_inDemo || g_screen != Screen::Playing || g_state->run.phase != RunPhase::Playing) return;
    if (rawDt > 0.25f) return;  // A hidden tab or a stall says nothing about the GPU.
    g_slowSeconds = g_frameMs > kSlowFrameMs ? g_slowSeconds + rawDt : 0.0f;
    if (g_slowSeconds > kSlowForSeconds) effects::setQuality(0);
}

// ---- Drawing --------------------------------------------------------------------

void drawDebugOverlay() {
    char text[160];
    std::snprintf(text, sizeof(text), "%d fps  frame %.1f ms  sim %.2f ms  draw %.2f ms\nenemies %d  shots %d  bullets %d  gems %d  events %d",
                  GetFPS(), static_cast<double>(g_frameMs), static_cast<double>(g_simMs), static_cast<double>(g_drawMs),
                  g_state->enemies.count, g_state->projectiles.count, g_state->enemyBullets.count, g_state->gems.count, g_state->events.count);
    font::drawShadowed(text, 12.0f, static_cast<float>(GetScreenHeight()) - 64.0f, 20.0f, theme::kDebugText);}

const char* screenName() {
    switch (g_screen) {
        case Screen::Title: return "title";
        case Screen::Playing: return g_state->run.phase == RunPhase::LevelUp ? "levelup" : "playing";
        case Screen::Paused: return "paused";
        case Screen::Settings: return "settings";
        case Screen::HowToPlay: return "howto";
        case Screen::Over: return "over";
    }
    return "";
}

}  // namespace

void init() {
    // This is a 2D game, and shapes are not all wound the same way.
    rlDisableBackfaceCulling();
    font::init();
    worldview::init();
    effects::init();
    g_state = new GameState();
    g_debug = platform::debugEnabled();
    audio::init();
    loadSave();
    // A phone has no keyboard to hint at, so its prompts are right from the first frame.
    if (platform::touchIsOnlyPointer()) g_input.lastDevice = input::Device::Touch;
    startDemo();
}

// WORLD PASS. Draws the game world from the simulation state, and nothing else. The
// renderer owns what happens inside, so it can draw to a low-resolution target and
// scale it up. The UI pass below always draws afterward, at full screen resolution.
static void drawWorldPass(float alpha) {
    worldview::draw(*g_state, alpha);
}

// UI PASS. Every screen, the HUD and the touch joystick, drawn on top of the world at
// full resolution. Screens are immediate mode: each one also reads its own input.
static void drawUiPass(float dtReal, bool playing) {
    const float screenFade = smoothstep01(g_screenAge / kScreenFadeSeconds);
    ui::setFade(1.0f);
    // The HUD belongs to play only. Menus and the level-up cards cover the world, and
    // the cards show the build themselves.
    if (!g_inDemo && g_screen == Screen::Playing && g_state->run.phase != RunPhase::LevelUp) {
        // The boss bar needs the space at the bottom, so the hint gives way to it.
        const float hintAlpha = g_state->run.bossSpawned ? 0.0f : 1.0f - smoothstep01((g_moveTime - 0.4f) / 0.6f);
        const ui::HudPresses presses = ui::drawHud(*g_state, hintAlpha, playing);
        if (presses.pause) pauseGame();
        else if (presses.dash) g_pendingDash = true;
        if (g_input.joystickActive && playing) ui::drawVirtualJoystick(g_input);
    }

    switch (g_screen) {
        case Screen::Title: {
            ui::setFade(screenFade);
            const ui::TitleAction action = ui::drawTitleScreen(save::current().best);
            if (action == ui::TitleAction::Play) startRun(0);
            else if (action == ui::TitleAction::Settings) openSettings(Screen::Title);
            else if (action == ui::TitleAction::HowToPlay) {
                ui::setInputEnabled(true);
                setScreen(Screen::HowToPlay);
            }
            break;
        }
        case Screen::Playing:
            if (g_state->run.phase == RunPhase::LevelUp) {
                ui::setFade(smoothstep01(g_phaseAge / kScreenFadeSeconds));
                const ui::LevelUpChoice choice = ui::drawLevelUpCards(*g_state, dtReal);
                if (choice.card >= 0) g_pendingChoice.card = choice.card;
                if (choice.reroll) g_pendingChoice.reroll = true;
            }
            break;
        case Screen::Paused: {
            ui::setFade(screenFade);
            const ui::PauseAction action = ui::drawPauseMenu(*g_state);
            if (action == ui::PauseAction::Resume) {
                audio::playUi(audio::UiSound::Back);
                setScreen(Screen::Playing);
            } else if (action == ui::PauseAction::Settings) openSettings(Screen::Paused);
            else if (action == ui::PauseAction::Restart) startRun(0);
            else if (action == ui::PauseAction::Quit) startDemo();
            break;
        }
        case Screen::Settings: {
            ui::setFade(screenFade);
            const ui::SettingsResult result = ui::drawSettingsScreen(save::current().settings, g_settingsFrom == Screen::Paused ? "Back to pause" : "Back");
            if (result.changed) {
                applySettings();
                g_settingsDirty = true;
            }
            if (result.back) leaveSettings();
            break;
        }
        case Screen::HowToPlay: {
            ui::setFade(screenFade);
            if (ui::drawHowToPlay()) {
                ui::resetTitleScreen();
                setScreen(Screen::Title);
            }
            break;
        }
        case Screen::Over: {
            const ui::GameOverAction action = ui::drawGameOverScreen(g_summary, g_screenAge);
            if (action == ui::GameOverAction::Restart) startRun(0);
            else if (action == ui::GameOverAction::Quit) startDemo();
            break;
        }
    }

    // A short fade from black when a run starts. It is only a picture: input already works.
    if (g_runStartFade > 0.0f) {
        g_runStartFade = std::max(0.0f, g_runStartFade - dtReal / kRunStartFadeSeconds);
        ui::setFade(1.0f);
        Color cover = theme::kFadeFromDark;
        cover.a = static_cast<unsigned char>(255.0f * smoothstep01(g_runStartFade));
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), cover);
    }
}

void frame() {
    platform::syncCanvasSize(1.0f);
    const float rawDt = GetFrameTime();
    g_frameMs = smooth(g_frameMs, rawDt * 1000.0f);
    const float dtReal = std::min(rawDt, 0.1f);

    // Input. The joystick only exists while playing, and a touch that starts on the
    // pause button must press it instead.
    const bool running = !g_inDemo && g_screen == Screen::Playing;
    const bool playing = running && g_state->run.phase == RunPhase::Playing;
    input::setJoystickEnabled(playing);
    const input::Rect blockers[2] = {ui::pauseButtonRect(), ui::dashButtonRect()};
    input::setTouchBlockers(blockers, 2);
    input::update(g_input);
    ui::beginFrame(g_input, dtReal);
    g_screenAge += dtReal;
    if (g_input.anyPress && !g_audioUnlocked) {
        audio::unlock();  // Browsers only allow sound after the first press.
        g_audioUnlocked = true;
    }

    // Leaving the tab or window pauses at once, so no damage is taken unseen.
    if (platform::consumeFocusLost()) pauseGame();

    // Back keys: pause while playing, and step back out of menus.
    if (g_input.back) {
        if (playing) pauseGame();
        else if (!g_inDemo && g_screen == Screen::Paused) {
            audio::playUi(audio::UiSound::Back);
            setScreen(Screen::Playing);
        } else if (g_screen == Screen::Settings) leaveSettings();
        else if (g_screen == Screen::HowToPlay) {
            audio::playUi(audio::UiSound::Back);
    // A dash press only counts while playing. Space is also confirm, so on a menu it must
    // not leave a dash waiting for the first moment the game runs.
    if (!playing) g_pendingDash = false;
    else if (g_input.dash) g_pendingDash = true;

            ui::resetTitleScreen();
            setScreen(Screen::Title);
        }
    }

    // A hit pause keeps back a little of this frame's time. The steps still run in the
    // same order with the same kind of input, only later, so runs stay deterministic.
    const float simDt = effects::filterSimulationTime(dtReal);
    const bool paused = simDt < dtReal;

    // Time only moves in a real run on the play screen, or in the title demo.
    const bool demoRuns = g_inDemo && g_screen != Screen::Over;
    if (g_inDemo) {
        if (demoRuns) runDemo(dtReal);
    } else if (g_screen == Screen::Playing) {
        if (g_showcase) worldview::tickShowcase(*g_state);
        else runSimulation(simDt);
        if (playing && std::hypot(g_input.moveX, g_input.moveY) > 0.2f) g_moveTime += dtReal;
    }

    // Events feed the sound and the effects. The demo makes no sound, and every frame's
    // list is emptied.
    if (!g_inDemo) {
        for (const SimEvent& event : g_state->events) audio::onEvent(event);
    }
    worldview::consumeEvents(*g_state);
    effects::consumeEvents(*g_state, g_inDemo);
    g_state->events.clear();

    // Particles and numbers wait with the world behind a menu or a hit pause. On the
    // game over screen they carry on, so a last explosion finishes behind the panel.
    effects::update(dtReal, !paused && (g_inDemo || g_screen == Screen::Playing || g_screen == Screen::Over));
    watchFrameRate(rawDt);

    // A run that just ended goes to its summary.
    if (!g_inDemo && g_screen == Screen::Playing && (g_state->run.phase == RunPhase::Dead || g_state->run.phase == RunPhase::Won)) finishRun();
    if (g_state->run.phase != g_lastPhase) {
        g_lastPhase = g_state->run.phase;
        g_phaseAge = 0.0f;
    }
    g_phaseAge += dtReal;

    const bool ducked = g_inDemo || g_screen != Screen::Playing || g_state->run.phase != RunPhase::Playing;
    if (ducked != g_ducked) {
        g_ducked = ducked;
        audio::setDucked(ducked);
    }
    audio::update(dtReal, std::min(1.0f, g_state->timeSeconds() / balance::kRunSeconds), g_state->run.bossSpawned);

    const double drawStart = GetTime();
    const float alpha = g_accumulator / kDt;
    BeginDrawing();
    ClearBackground(theme::kBackground);

    // WORLD PASS, then UI PASS. Keep them separate.
    drawWorldPass(alpha);
    drawUiPass(dtReal, playing);

    if (g_debug) drawDebugOverlay();

    // Settings are saved once a change is finished, not on every frame of a drag.
    if (g_settingsDirty && !g_input.pointerDown) persist();

    g_drawMs = smooth(g_drawMs, static_cast<float>((GetTime() - drawStart) * 1000.0));
    EndDrawing();
}

const char* queryState() {
    static char json[512];
    const GameState& s = *g_state;
    std::snprintf(json, sizeof(json),
                  "{\"phase\":%d,\"tick\":%u,\"kills\":%d,\"level\":%d,\"hp\":%.1f,\"x\":%.1f,\"y\":%.1f,"
                  "\"enemies\":%d,\"shots\":%d,\"bullets\":%d,\"gems\":%d,\"offers\":%d,\"rerolls\":%d,"
                  "\"fps\":%d,\"frameMs\":%.2f,\"simMs\":%.3f,\"drawMs\":%.3f,\"width\":%d,\"height\":%d,\"dashLeft\":%.3f,\"dashCooldown\":%.2f,"
                  "\"dashX\":%.3f,\"dashY\":%.3f,\"difficulty\":%d,\"hash\":\"%llx\"}",
                  static_cast<int>(s.run.phase), s.run.tick, s.run.kills, s.player.level, static_cast<double>(s.player.hp),
                  static_cast<double>(s.player.pos.x), static_cast<double>(s.player.pos.y), s.enemies.count, s.projectiles.count,
                  s.enemyBullets.count, s.gems.count, s.run.offerCount, s.run.rerollsLeft, GetFPS(),
                  static_cast<double>(g_frameMs), static_cast<double>(g_simMs), static_cast<double>(g_drawMs),
                  GetScreenWidth(), GetScreenHeight(), static_cast<double>(s.player.dashLeft), static_cast<double>(s.player.dashCooldown),
                  static_cast<double>(s.player.dashDir.x), static_cast<double>(s.player.dashDir.y), static_cast<int>(s.run.difficulty),
                  static_cast<unsigned long long>(sim::stateHash(s)));
    return json;
}

// What the screens look like right now, for the browser tests: which screen, the
// settings, the best run, and the position of every button in drawing buffer pixels.
const char* queryUi() {
    static char json[4096];
    const Settings& st = save::current().settings;
    const BestRun& best = save::current().best;
    const char* devices[] = {"keyboard", "gamepad", "touch", "mouse"};
    const platform::SafeInsets insets = platform::safeInsets();
    int n = std::snprintf(json, sizeof(json),
                          "{\"screen\":\"%s\",\"demo\":%d,\"device\":\"%s\",\"width\":%d,\"height\":%d,\"unit\":%.3f,"
                          "\"music\":%.3f,\"effects\":%.3f,\"shake\":%.3f,\"flash\":%.3f,\"damageNumbers\":%d,\"reducedMotion\":%d,"
                          "\"bestTime\":%.1f,\"bestKills\":%d,\"bestLevel\":%d,\"wins\":%d,\"runs\":%d,"
                          "\"joystick\":%d,\"won\":%d,\"newBest\":%d,\"moveTime\":%.2f,\"safe\":[%.1f,%.1f,%.1f,%.1f],"
                          "\"safeRect\":[%.1f,%.1f,%.1f,%.1f],\"selected\":\"%s\",\"buttons\":[",
                          screenName(), g_inDemo ? 1 : 0, devices[static_cast<int>(g_input.lastDevice)], GetScreenWidth(), GetScreenHeight(),
                          static_cast<double>(ui::u()), static_cast<double>(st.musicVolume), static_cast<double>(st.effectsVolume),
                          static_cast<double>(st.screenShake), static_cast<double>(st.flashIntensity), st.damageNumbers ? 1 : 0,
                          st.reducedMotion ? 1 : 0, static_cast<double>(best.bestTimeSeconds), best.mostKills, best.highestLevel, best.wins,
                          best.runs, g_input.joystickActive ? 1 : 0, g_summary.won ? 1 : 0,
                          (g_summary.newBestTime || g_summary.newBestKills || g_summary.newBestLevel) ? 1 : 0, static_cast<double>(g_moveTime),
                          static_cast<double>(insets.left), static_cast<double>(insets.top), static_cast<double>(insets.right),
                          static_cast<double>(insets.bottom), static_cast<double>(ui::layout().left), static_cast<double>(ui::layout().top),
                          static_cast<double>(ui::layout().right), static_cast<double>(ui::layout().bottom), ui::selectedName());
    for (int i = 0; i < ui::registeredCount() && n < static_cast<int>(sizeof(json)) - 160; ++i) {
        const ui::Registered& r = ui::registered(i);
        n += std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "%s{\"n\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f}",
                           i > 0 ? "," : "", r.name, static_cast<double>(r.rect.x), static_cast<double>(r.rect.y),
                           static_cast<double>(r.rect.width), static_cast<double>(r.rect.height));
    }
    std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "]}");
    return json;
}

void debugCommand(int command, int value) {
    if (!g_debug) return;
    GameState& s = *g_state;
    // Any other debug command hands control back to the simulation, except the ones
    // that only move the camera or the clock, which also work while it is held still.
    const bool keepsShowcase = command == kSetTime || command == kMovePlayerX || command == kMovePlayerY || command == kSetQuality || command == kSetShake ||
                               command == kSetFlash || command == kSetDamageNumbers || command == kSetReducedMotion || command == kPushEvent || command == kResetFxCounters || command == kSetMap;
    if (!keepsShowcase) g_showcase = command == kShowcase;
    switch (command) {
        case kShowcase: worldview::setupShowcase(s, value); break;
        case kMovePlayerX: s.player.pos.x = s.player.prevPos.x = static_cast<float>(value); break;
        case kMovePlayerY: s.player.pos.y = s.player.prevPos.y = static_cast<float>(value); break;
        case kSetQuality:
            worldview::setQuality(value);
            effects::setQuality(value);
            g_qualityPinned = true;
            break;
        case kSetShake:
            save::current().settings.screenShake = static_cast<float>(value) / 100.0f;
            applySettings();
            break;
        case kSetFlash:
            save::current().settings.flashIntensity = static_cast<float>(value) / 100.0f;
            applySettings();
            break;
        case kSetDamageNumbers:
            save::current().settings.damageNumbers = value != 0;
            applySettings();
            break;
        case kSetReducedMotion:
            save::current().settings.reducedMotion = value != 0;
            applySettings();
            break;
        case kPushEvent: {
            const auto type = static_cast<EventType>(value);
            const float size = type == EventType::Explosion ? 120.0f : (type == EventType::BombBlast ? balance::kBombRadius : 0.0f);
            pushEvent(s, type, s.player.pos, size);
            break;
        }
        case kResetFxCounters: effects::resetDebugCounters(); break;
        case kRunSteps:
            for (int i = 0; i < value; ++i) {
                // A dash asked for with kDash starts on the first of these steps.
                InputFrame input;
                input.dash = g_pendingDash;
                g_pendingDash = false;
                sim::step(s, input);
                effects::consumeEvents(s, false);
                effects::update(kDt, true);
                s.events.clear();
            }
            break;
        case kSetMap: maps::setCurrent(maps::mapFromIndex(value)); break;
        case kStressEnemies: sim::setStressTargets(s, value, s.run.debugProjectileTarget); break;
        case kStressProjectiles: sim::setStressTargets(s, s.run.debugEnemyTarget, value); break;
        case kSetSeed:
            g_seedOverride = static_cast<uint64_t>(value);
            startRun(g_seedOverride);
            break;
        case kGrantXp: s.player.xp = std::max(0, s.player.xpToNext - value); break;
        case kStartRun: startRun(static_cast<uint64_t>(std::max(0, value))); break;
        case kForceDeath:
            if (!g_inDemo) {
                s.player.hp = 0.0f;
                s.run.phase = RunPhase::Dead;
                pushEvent(s, EventType::PlayerDied, s.player.pos);
            }
            break;
        case kForceVictory:
            if (!g_inDemo) s.run.phase = RunPhase::Won;
            break;
        case kShowScreen:
            if (value == 0) startDemo();
            else if (value == 1 && !g_inDemo) {
                setScreen(Screen::Playing);
                pauseGame();
            } else if (value == 2) openSettings(g_inDemo ? Screen::Title : Screen::Paused);
            else if (value == 3) setScreen(Screen::HowToPlay);
            break;
        case kSetSafeArea: platform::setSafeInsetOverride(static_cast<float>(value)); break;
        case kGiveBuild: {
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) weapons::grant(s, static_cast<WeaponId>(i));
            for (int i = 0; i < s.player.weaponCount; ++i) {
                WeaponSlot& slot = s.player.weapons[i];
                slot.level = static_cast<uint8_t>(std::min(kMaxLevelPerItem, 1 + (i * 2) % 5 + (i == 0 ? 4 : 0)));
                weapons::refill(s, slot);
                slot.magazine = std::max(0, slot.magazine - i * 3);
            }
            if (s.player.weaponCount > 0) s.player.weapons[0].evolved = true;
            if (s.player.weaponCount > 2) s.player.weapons[2].reloadLeft = 0.5f;
            if (value == 1 && s.player.weaponCount > 1) {
                s.player.weapons[1].magazine = 0;
                s.player.weapons[1].reserve = 0;
                s.player.weapons[1].reloadLeft = 0.0f;
            }
            s.player.passiveCount = 0;
            for (int i = 0; i < static_cast<int>(PassiveId::Count); ++i) {
                PassiveSlot& passive = s.player.passives[s.player.passiveCount++];
                passive.id = static_cast<PassiveId>(i);
                passive.level = static_cast<uint8_t>(1 + (i * 2) % 5);
            }
            upgrades::recomputeStats(s);
            break;
        }
        case kSetHp: s.player.hp = s.player.maxHp * static_cast<float>(value) / 100.0f; break;
        case kSetTime: s.run.tick = static_cast<uint32_t>(std::max(0, value)) * 60u; break;
        case kSetKills: s.run.kills = value; break;
        case kSetLevel: s.player.level = std::max(1, value); break;
        case kFakeDamage:
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) {
                s.run.damageByWeapon[i] = i < 7 ? static_cast<float>(value) * static_cast<float>((i * 7919) % 13 + 2) / 13.0f : 0.0f;
            }
            break;
        case kLevelUp:
            if (!g_inDemo) upgrades::addXp(s, std::max(1, s.player.xpToNext - s.player.xp));
            break;
        default: break;
    }
}

}  // namespace ls::app

#ifdef __EMSCRIPTEN__
extern "C" {
// Test hook: what the screens look like and where their buttons are.
EMSCRIPTEN_KEEPALIVE const char* ls_query_ui() { return ls::app::queryUi(); }
// Test hook: counters that prove each effect and setting really changes behavior.
EMSCRIPTEN_KEEPALIVE const char* ls_query_effects() { return ls::effects::debugJson(); }
}
#endif
        case kDash: g_pendingDash = true; break;
        case kHoldSim: g_holdSim = value != 0; break;
        case kSetDashCooldown: s.player.dashCooldown = balance::kDashCooldownSeconds * static_cast<float>(value) / 100.0f; break;
