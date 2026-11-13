#include "core/App.h"

#include "components/Audio/Audio.h"
#include "components/Effects/Effects.h"
#include "components/Input/Input.h"
#include "components/Maps/Maps.h"
#include "components/Net/Net.h"
#include "components/NetGame/NetGame.h"
#include "components/Ui/CoopHud/CoopHud.h"
#include "components/Ui/CoopScreens/CoopScreens.h"
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
    kShowCarBoxes = 43,  // 1 outlines each car's solid box over the picture.
    kCarCover = 44,      // 0 stops cars hiding what stands behind them, for comparing pictures.
    kCrushNearestCar = 45,  // Flattens the car nearest the soldier.
    kBossBesideCar = 46,    // The boss about to walk over a car, with the simulation running.
    kKillBoss = 47,         // Takes the boss's HP away, so the next step kills it as a last hit would.
    kLevelUpSlot = 48,      // Co-op host: gives the player in this slot the XP for their next level.
};

// Which screen is showing. Level-up is not here: it is a phase of a running game.
enum class Screen { Title, Playing, Paused, Settings, HowToPlay, Over, Coop };

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
bool g_audioTried = false;     // A press has started the audio attempt.
bool g_audioUnlocked = false;  // Sound is really running.
bool g_settingsDirty = false;
bool g_ducked = true;  // The title screen is a menu, so music starts ducked. Audio starts the same way.
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

// Co-op. The host's tab being hidden pauses everyone, and the browser may not draw another
// frame until it comes back, so this is set straight from the page's event.
bool g_hostAway = false;
bool g_netOverlay = false;       // The network debug overlay, toggled with F3.
bool g_cardsWereOpen = false;    // Co-op cards open over the running game, so their delay restarts each time.
uint16_t g_cardsSerial = 0;      // The set of cards on screen, so a new set restarts the delay too.
bool g_slotConnected[kMaxPlayers] = {true, true, true, true};
// How many times each player of the run has come back from away, as last seen. A change means
// they came back, even if they also went away between two frames (a seat taken over in
// another tab), so the host still gives them a fresh start.
int g_slotReturns[kMaxPlayers] = {};
// The host tells a player who rejoined after another run began that they are not in it, once
// in a while until they leave. The clock counts down to the next time.
float g_notInRunClock = 0.0f;
char g_netDebug[640] = "";

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

// Reads the stored save again just before writing. Another tab or the installed app may
// have saved a better run since this copy loaded, so those records are merged in and
// never go backward. If the stored save can't be read (storage blocked, file locked) it
// may still be good, so nothing is written over it: this change is kept in memory and
// goes out with the next save.
void persist() {
    g_settingsDirty = false;
    std::string stored;
    const platform::SaveRead read = platform::readSave(&stored);
    if (read == platform::SaveRead::Unreadable) return;
    if (read == platform::SaveRead::Found) save::mergeBestRuns(save::current(), save::parse(stored));
    platform::writeSave(save::serialize(save::current()));
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
    if (platform::readSave(&text) == platform::SaveRead::Found) save::current() = save::parse(text, defaults);
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
// The title's choice of map shows in the demo behind it, and in the next run.
maps::MapId selectedMap() { return maps::mapFromIndex(save::current().map); }

void startDemo() {
    worldview::setFixedDayTime(theme::kDuskSeconds);
    effects::reset();
    g_state->localSlot = 0;
    sim::newRun(*g_state, kDemoSeed, Difficulty::Normal, selectedMap());
    for (WeaponId id : {WeaponId::Shotgun, WeaponId::Smg, WeaponId::Flamethrower, WeaponId::Tesla, WeaponId::Blades}) {
        weapons::grant(*g_state, 0, id);
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

bool coopRun() { return netgame::role() != netgame::Role::None; }
bool coopHost() { return netgame::role() == netgame::Role::Host; }
bool coopGuest() { return netgame::role() == netgame::Role::Client; }

// Everything a new run on screen needs, after its state has been made.
void showNewRun() {
    worldview::setFixedDayTime(-1.0f);
    effects::reset();
    ui::coopHudClearNotice();
    g_inDemo = false;
    g_accumulator = 0.0f;
    g_pendingChoice = ui::LevelUpChoice{};
    g_pendingDash = false;
    g_holdSim = false;
    g_moveTime = 0.0f;
    g_runStartFade = 1.0f;
    g_lastPhase = RunPhase::Playing;
    g_phaseAge = 0.0f;
    g_cardsWereOpen = false;
    ui::resetLevelUpCards();
    setScreen(Screen::Playing);
}

const net::LobbyPlayer* lobbyPlayer(int netId);

// Whether this lobby slot has a player who is connected now (not away).
bool lobbyHas(int netId) {
    const net::LobbyPlayer* player = lobbyPlayer(netId);
    return player && net::isHere(*player);
}

int lobbyReturns(int netId) {
    const net::LobbyPlayer* player = lobbyPlayer(netId);
    return player ? player->returns : 0;
}

// The names and colors of a co-op run's players, from the lobby, kept for the whole run.
void showNewCoopRun() {
    const net::Lobby& lobby = net::lobby();
    const char* names[kMaxPlayers] = {};
    int colors[kMaxPlayers] = {0, 1, 2, 3};
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        const int id = netgame::netIdOf(slot);
        g_slotConnected[slot] = true;
        g_slotReturns[slot] = 0;
        if (id < 0) continue;
        colors[slot] = id;
        // A guest who rejoined a run in progress may find a teammate away already. Starting
        // from what the lobby says now means no notice about something that did not just happen.
        g_slotConnected[slot] = lobbyHas(id);
        g_slotReturns[slot] = lobbyReturns(id);
        for (int i = 0; i < lobby.count; ++i) {
            if (lobby.players[i].id == id) names[slot] = lobby.players[i].name;
        }
    }
    ui::coopHudBeginRun(names, colors);
    worldview::setPlayerColors(colors);
    showNewRun();
}

// A co-op run's settings, made from the room as it is now. The host is slot 0, and the others
// follow in the lobby's order, so a player who has left, or is away, is not in the run.
netgame::RunStart coopSettingsFromLobby(uint64_t seed) {
    const net::Lobby& lobby = net::lobby();
    netgame::RunStart start;
    start.runNumber = 1;
    start.seed = seed;
    start.map = selectedMap();
    start.difficulty = save::current().difficulty;
    start.mode = save::current().mode;
    start.netIds[0] = static_cast<uint8_t>(lobby.players[lobby.host >= 0 ? lobby.host : 0].id);
    start.playerCount = 1;
    for (int i = 0; i < lobby.count && start.playerCount < kMaxPlayers; ++i) {
        if (i != lobby.host && net::isHere(lobby.players[i])) start.netIds[start.playerCount++] = static_cast<uint8_t>(lobby.players[i].id);
    }
    return start;
}

// The host pressed Start: this machine runs the real game, with the lobby's players in it.
void startCoopHost() {
    netgame::hostStart(*g_state, coopSettingsFromLobby(pickSeed()));
    showNewCoopRun();
}

// Leaving a co-op run on purpose: out of the room, and back to the title. This is leaving for
// good, so the seat saved for a rejoin is forgotten too and the Rejoin button does not appear.
void leaveCoop() {
    net::leaveRoom();
    net::forgetRejoin();
    netgame::stop();
    g_hostAway = false;
}

void startRun(uint64_t seed) {
    if (coopHost()) {
        const uint64_t next = seed != 0 ? seed : pickSeed();
        // The co-op host's Restart starts another run for whoever is still in the room. A
        // player who is away right now is left out: they may never come back.
        if (net::hereCount() >= 2) {
            netgame::hostRestart(*g_state, coopSettingsFromLobby(next));
            showNewCoopRun();
            return;
        }
        // Everyone else has left, so the next run is an ordinary single-player one. Leaving the
        // room closes it, so anyone away who tries to rejoin is told the room has closed.
        leaveCoop();
        g_state->localSlot = 0;
        sim::newRun(*g_state, next, save::current().difficulty, selectedMap(), save::current().mode);
        showNewRun();
        ui::coopHudNotice("Everyone else left. Playing solo.");
        return;
    }
    g_state->localSlot = 0;
    sim::newRun(*g_state, seed != 0 ? seed : pickSeed(), save::current().difficulty, selectedMap(), save::current().mode);
    showNewRun();
}

void pauseGame() {
    if (g_inDemo || g_screen != Screen::Playing || g_state->run.phase != RunPhase::Playing) return;
    ui::resetPauseMenu();
    audio::playUi(audio::UiSound::Pause);
    setScreen(Screen::Paused);
    // Only the host's pause stops the run. Everyone else is told at once.
    if (coopHost()) netgame::hostSendPause(true, g_hostAway);
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
    g_summary.difficulty = s.run.difficulty;
    g_summary.mode = s.run.mode;
    g_summary.timeSeconds = s.timeSeconds();
    g_summary.bossesKilled = s.run.bossesKilled;
    g_summary.kills = s.run.kills;
    g_summary.level = s.localPlayer().level;
    for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) g_summary.damageByWeapon[i] = s.run.damageByWeapon[i];
    if (coopRun()) {
        // Each player sees their own share of the damage, and a co-op run is never a best run:
        // a team of four should not beat a record set alone.
        g_summary.coop = true;
        g_summary.canRestart = coopHost();
        if (coopGuest() && netgame::clientRunEnded()) {
            const netgame::RunEnd& end = netgame::clientRunEnd();
            g_summary.won = end.phase == RunPhase::Won;
            g_summary.timeSeconds = static_cast<float>(end.tick) * kDt;
            g_summary.kills = end.kills;
            g_summary.bossesKilled = end.bossesKilled;
            g_summary.level = end.levels[s.localSlot];
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) g_summary.damageByWeapon[i] = end.damageByWeapon[s.localSlot][i];
        } else {
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) g_summary.damageByWeapon[i] = s.localPlayer().damageByWeapon[i];
        }
        ui::resetGameOverScreen();
        setScreen(Screen::Over);
        return;
    }

    // Best runs are kept per level and mode, so a new best only means something on the same
    // level and mode.
    save::FinishedRun finished;
    finished.mode = s.run.mode;
    finished.difficulty = s.run.difficulty;
    finished.timeSeconds = g_summary.timeSeconds;
    finished.kills = g_summary.kills;
    finished.level = g_summary.level;
    finished.bossesKilled = g_summary.bossesKilled;
    finished.won = g_summary.won;
    const save::NewBests beaten = save::recordRun(save::current(), finished);
    g_summary.newBestTime = beaten.time;
    g_summary.newBestKills = beaten.kills;
    g_summary.newBestLevel = beaten.level;
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
    // A co-op host choosing a card stands still: the keys move the selection, not the soldier.
    const bool choosing = coopRun() && g_state->localPlayer().cardsOpen;
    frameInput.moveX = choosing ? 0.0f : g_input.moveX;
    frameInput.moveY = choosing ? 0.0f : g_input.moveY;

    int steps = 0;
    while (g_accumulator >= kDt && steps < kMaxStepsPerFrame) {
        // A menu choice applies once, even when this frame runs several steps.
        frameInput.chooseCard = static_cast<int8_t>(g_pendingChoice.card);
        frameInput.reroll = g_pendingChoice.reroll;
        g_pendingChoice = ui::LevelUpChoice{};
        // One press is one dash: the first step of the frame takes it, the others don't.
        frameInput.dash = g_pendingDash && !choosing;
        g_pendingDash = false;
        if (coopHost()) {
            // The host's own input is slot 0. Everyone else's comes from the network.
            InputFrame inputs[kMaxPlayers];
            inputs[0] = frameInput;
            netgame::hostFillInputs(*g_state, inputs, g_state->playerCount());
            netgame::hostBeforeStep(*g_state);
            sim::step(*g_state, inputs, g_state->playerCount());
        } else {
            sim::step(*g_state, frameInput);
        }
        g_accumulator -= kDt;
        steps++;
    }
    if (steps == kMaxStepsPerFrame) g_accumulator = 0.0f;
    g_lastSteps = steps;
    // Each step's time, not each frame's, so numbers compare across refresh rates.
    if (steps > 0) g_simMs = smooth(g_simMs, static_cast<float>((GetTime() - start) * 1000.0) / static_cast<float>(steps));
}

// The scripted bot for the title screen: walks a wandering loop and never takes damage.
void runDemo(float dtReal) {
    g_accumulator += dtReal;
    g_demoAmmoTimer += dtReal;
    if (g_demoAmmoTimer > 4.0f) {
        g_demoAmmoTimer = 0.0f;
        weapons::addAmmoFraction(*g_state, 0, 1.0f);  // Keeps the guns firing.
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
// half the particles. Only frames that ran a fight can count against it, and the menus
// show what this screen's normal rate is. See FrameWatch for how the two are compared.
bool g_qualityPinned = false;               // Debug only: a test chose the quality itself.

void watchFrameRate(float rawDt) {
    if (g_qualityPinned) return;
    effects::watchFrameTime(rawDt, !g_inDemo && g_screen == Screen::Playing && g_state->run.phase == RunPhase::Playing);
}

// ---- Co-op during the run ---------------------------------------------------------

const net::LobbyPlayer* lobbyPlayer(int netId) {
    const net::Lobby& lobby = net::lobby();
    for (int i = 0; i < lobby.count; ++i) {
        if (lobby.players[i].id == netId) return &lobby.players[i];
    }
    return nullptr;
}

// The round trip to the host, or for the host the slowest player, in milliseconds.
int coopPing() {
    int worst = -1;
    for (int slot = 0; slot < g_state->playerCount(); ++slot) {
        if (slot == g_state->localSlot) continue;
        if (coopGuest() && slot != 0) continue;
        const net::LobbyPlayer* player = lobbyPlayer(netgame::netIdOf(slot));
        if (player && player->rttMs > worst) worst = player->rttMs;
    }
    return worst;
}

const char* coopTransport() {
    const int target = coopGuest() ? 0 : (g_state->playerCount() > 1 ? 1 : 0);
    const net::LobbyPlayer* player = lobbyPlayer(netgame::netIdOf(target));
    return player ? net::transportName(player->transport) : "none";
}

// The room is gone: the host left, or the server was lost. The run ends here, and the co-op
// screens show why (they read the reason from the lobby).
void endCoopToLobby() {
    netgame::stop();
    g_hostAway = false;
    startDemo();
    setScreen(Screen::Coop);
}

// The host, now and then: anyone in the room who is connected but has no soldier in the
// current run came back after another run began without them, so they are told.
void tellPlayersNotInRun(float dt) {
    g_notInRunClock -= dt;
    if (g_notInRunClock > 0.0f) return;
    g_notInRunClock = balance::kNetResendSeconds;
    const net::Lobby& lobby = net::lobby();
    for (int i = 0; i < lobby.count; ++i) {
        const net::LobbyPlayer& player = lobby.players[i];
        if (i == lobby.host || !net::isHere(player) || netgame::slotOfNetId(player.id) >= 0) continue;
        netgame::hostSendNotInRun(player.id);
    }
}

// Once a frame, before anything is stepped: starts a co-op run when the host presses Start,
// notices when the room closes, and keeps track of who is still connected.
void watchCoop(float dt) {
    const net::Lobby& lobby = net::lobby();
    if (!coopRun()) {
        if (g_screen == Screen::Coop && lobby.phase == net::LobbyPhase::Starting) {
            if (lobby.you >= 0 && lobby.you == lobby.host) startCoopHost();
            else netgame::clientListen();
        }
        return;
    }
    // While a guest reconnects by itself the run stays on screen: the room is not gone yet.
    const bool reconnecting = lobby.phase == net::LobbyPhase::Reconnecting;
    if (lobby.phase != net::LobbyPhase::Starting && !reconnecting) {
        endCoopToLobby();
        return;
    }
    if (coopHost()) {
        netgame::hostReceive();
        tellPlayersNotInRun(dt);
    }
    if (coopGuest() && netgame::clientNotInRun()) {
        // The host started another run while this player was away. Back to the co-op menu, which
        // shows why; the saved seat is forgotten, so the Rejoin button goes away.
        net::abandonRun(net::LobbyError::RunEnded);
        endCoopToLobby();
        return;
    }
    if (coopGuest() && netgame::clientNewRun(*g_state)) showNewCoopRun();
    // While reconnecting, this machine's copy of the room is old, so it says nothing new.
    if (g_inDemo || reconnecting) return;
    // A player missing from the room, or away, has left. The host keeps them in the run, down
    // and still. One who comes back gets the run again from the host.
    for (int slot = 0; slot < g_state->playerCount(); ++slot) {
        if (slot == g_state->localSlot) continue;
        const int id = netgame::netIdOf(slot);
        const bool here = lobbyHas(id);
        const int returns = lobbyReturns(id);
        const bool back = here && (!g_slotConnected[slot] || returns != g_slotReturns[slot]);
        g_slotReturns[slot] = returns;
        if (here == g_slotConnected[slot] && !back) continue;
        g_slotConnected[slot] = here;
        const char* name = ui::coopHudName(slot)[0] ? ui::coopHudName(slot) : "A player";
        char message[64];
        if (back) {
            if (coopHost()) netgame::hostPlayerBack(slot);
            std::snprintf(message, sizeof(message), "%s is back", name);
        } else {
            if (coopHost()) netgame::hostSetConnected(slot, false);
            std::snprintf(message, sizeof(message), "%s left the run", name);
        }
        ui::coopHudNotice(message);
    }
}

// Events about another player are theirs to hear and see: a teammate's level-up or pickup
// must not sound like this player's own. Events about nobody (a zombie hit) stay, and so do
// the ones everyone should know about, like a teammate going down.
void keepEventsForThisPlayer() {
    if (!coopRun()) return;
    const int local = g_state->localSlot;
    int kept = 0;
    for (int i = 0; i < g_state->events.count; ++i) {
        const SimEvent& event = g_state->events[i];
        const bool shared = event.type == EventType::PlayerDowned || event.type == EventType::PlayerRevived || event.type == EventType::PlayerDied;
        if (event.player != kNoPlayer && event.player != local && !shared) continue;
        g_state->events.items[kept++] = event;
    }
    g_state->events.count = kept;
}

void buildNetDebug() {
    const netgame::Stats& st = netgame::stats();
    int n = std::snprintf(g_netDebug, sizeof(g_netDebug), "NET %s  slot %d of %d  via %s\nping %d ms  server %d ms\nsnapshots out %d/s  in %d/s  age %.0f ms\nsnapshot %d B  avg %d  max %d\nmessages out %d/s  in %d/s\nbytes out %.1f KB/s  in %.1f KB/s\nprediction fixes %d  jumps %d  last %.1f",
                          coopHost() ? "host" : "guest", g_state->localSlot, g_state->playerCount(), coopTransport(), coopPing(), net::lobby().serverRttMs,
                          st.snapshotsOutPerSec, st.snapshotsInPerSec, static_cast<double>(st.snapshotAge * 1000.0f), st.lastSnapshotBytes,
                          st.avgSnapshotBytes, st.maxSnapshotBytes, st.messagesOutPerSec, st.messagesInPerSec,
                          static_cast<double>(st.bytesOutPerSec) / 1024.0, static_cast<double>(st.bytesInPerSec) / 1024.0, st.corrections, st.snaps,
                          static_cast<double>(st.predictionError));
    // Each other player's own line, as the relay and the browser count them.
    for (int slot = 0; slot < g_state->playerCount() && n > 0 && n < static_cast<int>(sizeof(g_netDebug)) - 80; ++slot) {
        if (slot == g_state->localSlot || (coopGuest() && slot != 0)) continue;
        const net::LobbyPlayer* player = lobbyPlayer(netgame::netIdOf(slot));
        if (!player) continue;
        n += std::snprintf(g_netDebug + n, sizeof(g_netDebug) - static_cast<size_t>(n), "\n%s: %s %d ms  %d/%d msg/s", ui::coopHudName(slot),
                           net::transportName(player->transport), std::max(0, player->rttMs), player->msgsOutPerSec, player->msgsInPerSec);
    }
}

void drawCoopOverlay(float dtReal) {
    ui::CoopHudView view;
    view.reconnecting = coopGuest() && net::lobby().phase == net::LobbyPhase::Reconnecting;
    view.hostAway = coopGuest() ? netgame::clientHostAway() : false;
    view.hostPaused = coopGuest() && netgame::clientHostPaused();
    view.pingMs = coopPing();
    view.snapshotAge = coopGuest() ? netgame::stats().snapshotAge : 0.0f;
    for (int slot = 0; slot < kMaxPlayers; ++slot) view.connected[slot] = g_slotConnected[slot];
    if (g_netOverlay) {
        buildNetDebug();
        view.debugText = g_netDebug;
    }
    ui::drawCoopHud(*g_state, view, dtReal);
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
        case Screen::Coop: return ui::coopScreenName();
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
    // A free co-op server sleeps, so it is woken now, while the title is read, rather than when
    // someone presses Host. In the fake backend this only moves the phase to Waking and back.
    net::wakeServer();
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
    const bool coopCards = coopRun() && !g_inDemo && g_screen == Screen::Playing && g_state->localPlayer().cardsOpen && g_state->run.phase == RunPhase::Playing;
    // A new set of cards (the next level's, or a reroll) gets its own short input delay too, so a
    // press meant for the old cards can't pick one of the new ones.
    const uint16_t serial = g_state->localPlayer().offerSerial;
    if (coopCards != g_cardsWereOpen || (coopCards && serial != g_cardsSerial)) {
        g_cardsWereOpen = coopCards;
        g_cardsSerial = serial;
        ui::resetLevelUpCards();
    }
    if (!g_inDemo && g_screen == Screen::Playing && g_state->run.phase != RunPhase::LevelUp && !coopCards) {
        // The boss bar needs the space at the bottom, so the hint gives way to it.
        const float hintAlpha = g_state->run.bossSpawned ? 0.0f : 1.0f - smoothstep01((g_moveTime - 0.4f) / 0.6f);
        const ui::HudPresses presses = ui::drawHud(*g_state, hintAlpha, playing);
        if (presses.pause) pauseGame();
        else if (presses.dash) g_pendingDash = true;
        // Always there on a touch screen, so the thumb knows where to go before it lands.
        if (playing && g_input.lastDevice == input::Device::Touch) ui::drawVirtualJoystick(g_input);
    }
    if (coopRun() && !g_inDemo && g_screen == Screen::Playing) drawCoopOverlay(dtReal);
    // A run that began as co-op and went on alone keeps its notice for its few seconds.
    else if (!coopRun() && !g_inDemo && g_screen == Screen::Playing) ui::drawCoopNotice(dtReal);

    switch (g_screen) {
        case Screen::Title: {
            ui::setFade(screenFade);
            const Difficulty before = save::current().difficulty;
            const int mapBefore = save::current().map;
            const RunMode modeBefore = save::current().mode;
            const ui::TitleAction action = ui::drawTitleScreen(save::current());
            // The choice is kept at once, so it is there next time even if the page closes now.
            if (save::current().difficulty != before || save::current().map != mapBefore || save::current().mode != modeBefore) persist();
            if (save::current().map != mapBefore) sim::setMap(*g_state, selectedMap());
            if (action == ui::TitleAction::Play) startRun(0);
            else if (action == ui::TitleAction::Settings) openSettings(Screen::Title);
            else if (action == ui::TitleAction::HowToPlay) {
                ui::setInputEnabled(true);
                setScreen(Screen::HowToPlay);
            } else if (action == ui::TitleAction::Coop) {
                ui::openCoopScreens(save::current());
                setScreen(Screen::Coop);
            }
            break;
        }
        case Screen::Coop: {
            ui::setFade(screenFade);
            const int mapBefore = save::current().map;
            const ui::CoopResult result = ui::drawCoopScreens(save::current());
            if (result.saveChanged) persist();
            // The host picked another map in the lobby, so the demo behind the screen changes too, as on the title.
            if (save::current().map != mapBefore) sim::setMap(*g_state, selectedMap());
            if (result.back) {
                audio::playUi(audio::UiSound::Back);
                setScreen(Screen::Title);
            }
            break;
        }
        case Screen::Playing:
            if (coopCards) {
                // Co-op: the cards open over the running game, and only this player waits.
                ui::setFade(1.0f);
                const ui::LevelUpChoice choice = ui::drawLevelUpCards(*g_state, dtReal);
                if (coopGuest()) {
                    netgame::clientPickCard(*g_state, choice.card, choice.reroll);
                } else if (g_pendingChoice.card < 0 && !g_pendingChoice.reroll) {
                    if (choice.card >= 0) g_pendingChoice.card = choice.card;
                    else if (choice.reroll) g_pendingChoice.reroll = true;
                }
            } else if (g_state->run.phase == RunPhase::LevelUp) {
                ui::setFade(smoothstep01(g_phaseAge / kScreenFadeSeconds));
                const ui::LevelUpChoice choice = ui::drawLevelUpCards(*g_state, dtReal);
                // One action until a step uses it. On a fast display several frames can pass
                // before a step runs, and a pick made after a reroll would be applied to
                // cards the player never saw.
                const bool actionWaiting = g_pendingChoice.card >= 0 || g_pendingChoice.reroll;
                if (!actionWaiting) {
                    if (choice.card >= 0) g_pendingChoice.card = choice.card;
                    else if (choice.reroll) g_pendingChoice.reroll = true;
                }
            }
            break;
        case Screen::Paused: {
            ui::setFade(screenFade);
            const ui::PauseKind kind = coopHost() ? ui::PauseKind::CoopHost : (coopGuest() ? ui::PauseKind::CoopGuest : ui::PauseKind::Solo);
            const ui::PauseAction action = ui::drawPauseMenu(*g_state, kind);
            if (action == ui::PauseAction::Resume) {
                audio::playUi(audio::UiSound::Back);
                setScreen(Screen::Playing);
            } else if (action == ui::PauseAction::Settings) openSettings(Screen::Paused);
            else if (action == ui::PauseAction::Restart && !coopGuest()) startRun(0);
            else if (action == ui::PauseAction::Quit) {
                if (coopRun()) leaveCoop();
                startDemo();
            }
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
            if (action == ui::GameOverAction::Restart && !coopGuest()) startRun(0);
            else if (action == ui::GameOverAction::Quit) {
                if (coopRun()) leaveCoop();
                startDemo();
            }
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
    net::update(dtReal);
    watchCoop(dtReal);

    // Input. The joystick only exists while playing, and a touch that starts on the
    // pause button must press it instead. A co-op player choosing a card stands still.
    const bool running = !g_inDemo && g_screen == Screen::Playing;
    const bool playing = running && g_state->run.phase == RunPhase::Playing && !(coopRun() && g_state->localPlayer().cardsOpen);
    input::setJoystickEnabled(playing);
    input::setJoystickHome(ui::joystickHome());
    const input::Rect blockers[2] = {ui::pauseButtonRect(), ui::dashButtonRect()};
    input::setTouchBlockers(blockers, 2);
    input::update(g_input);
    ui::beginFrame(g_input, dtReal);
    g_screenAge += dtReal;
    // Browsers only allow sound after a press, and some presses (Esc, a gamepad button) do
    // not count. So the first press starts the attempt, and every later press tries again
    // until sound is really running.
    if (!g_audioUnlocked && (g_audioTried || g_input.anyPress)) {
        g_audioTried = true;
        g_audioUnlocked = audio::unlock(g_input.anyPress);
    }

    // A dash press only counts while playing. Space is also confirm, so on a menu it must
    // not leave a dash waiting for the first moment the game runs.
    if (!playing) g_pendingDash = false;
    else if (g_input.dash) g_pendingDash = true;

    // Leaving the tab or window pauses at once, so no damage is taken unseen. In co-op only
    // the host can pause, because the host's pause stops the run for everyone.
    if (platform::consumeFocusLost() && !coopGuest()) pauseGame();
    if (g_input.netOverlay) g_netOverlay = !g_netOverlay;

    // Back keys: pause while playing, and step back out of menus.
    if (g_input.back) {
        // Esc pauses while choosing a co-op card too, because the run goes on behind the cards.
        if (playing || (running && g_state->run.phase == RunPhase::Playing)) pauseGame();
        else if (!g_inDemo && g_screen == Screen::Paused) {
            audio::playUi(audio::UiSound::Back);
            setScreen(Screen::Playing);
        } else if (g_screen == Screen::Settings) leaveSettings();
        else if (g_screen == Screen::HowToPlay) {
            audio::playUi(audio::UiSound::Back);
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
    } else if (coopGuest()) {
        // A guest never steps the simulation. The host's snapshots fill the state, and only
        // this player's own soldier is predicted, from the same input the host will get.
        netgame::clientFrame(*g_state, dtReal, playing ? g_input.moveX : 0.0f, playing ? g_input.moveY : 0.0f, playing && g_pendingDash);
        g_pendingDash = false;
        if (playing && std::hypot(g_input.moveX, g_input.moveY) > 0.2f) g_moveTime += dtReal;
    } else if (g_screen == Screen::Playing) {
        if (g_showcase) worldview::tickShowcase(*g_state);
        else if (!g_holdSim) runSimulation(simDt);
        if (playing && std::hypot(g_input.moveX, g_input.moveY) > 0.2f) g_moveTime += dtReal;
    }

    // The host sends this frame's world to the others before the events are cleared, because
    // each player is told about the events near them.
    if (coopHost() && !g_inDemo) netgame::hostAfterFrame(*g_state, dtReal, g_screen != Screen::Playing || g_hostAway, g_hostAway);

    // Events feed the sound and the effects. The demo makes no sound, and every frame's
    // list is emptied. The soldiers see every player's shots first. Then, in co-op, what
    // happened to other players is taken out, so this player only hears their own.
    worldview::consumeEvents(*g_state);
    keepEventsForThisPlayer();
    if (!g_inDemo) {
        for (const SimEvent& event : g_state->events) audio::onEvent(event);
    }
    effects::consumeEvents(*g_state, g_inDemo);
    g_state->events.clear();

    // Particles and numbers wait with the world behind a menu or a hit pause. On the
    // game over screen they carry on, so a last explosion finishes behind the panel.
    effects::update(dtReal, !paused && (g_inDemo || g_screen == Screen::Playing || g_screen == Screen::Over));
    watchFrameRate(rawDt);

    // A run that just ended goes to its summary. A guest waits for the host's results.
    const bool ended = g_state->run.phase == RunPhase::Dead || g_state->run.phase == RunPhase::Won;
    if (!g_inDemo && (g_screen == Screen::Playing || g_screen == Screen::Paused) && (coopGuest() ? netgame::clientRunEnded() : ended)) finishRun();
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
    const float alpha = coopGuest() && !g_inDemo ? netgame::clientAlpha() : g_accumulator / kDt;
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
    static char json[640];
    const GameState& s = *g_state;
    std::snprintf(json, sizeof(json),
                  "{\"phase\":%d,\"tick\":%u,\"kills\":%d,\"level\":%d,\"hp\":%.1f,\"x\":%.1f,\"y\":%.1f,"
                  "\"enemies\":%d,\"shots\":%d,\"bullets\":%d,\"gems\":%d,\"offers\":%d,\"rerolls\":%d,"
                  "\"fps\":%d,\"frameMs\":%.2f,\"simMs\":%.3f,\"drawMs\":%.3f,\"width\":%d,\"height\":%d,\"dashLeft\":%.3f,\"dashCooldown\":%.2f,"
                  "\"dashX\":%.3f,\"dashY\":%.3f,\"difficulty\":%d,\"mode\":%d,\"bossesKilled\":%d,\"bossSpawned\":%d,"
                  "\"bossHp\":%.3f,\"hash\":\"%llx\"}",
                  static_cast<int>(s.run.phase), s.run.tick, s.run.kills, s.firstPlayer().level, static_cast<double>(s.firstPlayer().hp),
                  static_cast<double>(s.firstPlayer().pos.x), static_cast<double>(s.firstPlayer().pos.y), s.enemies.count, s.projectiles.count,
                  s.enemyBullets.count, s.gems.count, s.firstPlayer().offerCount, s.firstPlayer().rerollsLeft, GetFPS(),
                  static_cast<double>(g_frameMs), static_cast<double>(g_simMs), static_cast<double>(g_drawMs),
                  GetScreenWidth(), GetScreenHeight(), static_cast<double>(s.firstPlayer().dashLeft), static_cast<double>(s.firstPlayer().dashCooldown),
                  static_cast<double>(s.firstPlayer().dashDir.x), static_cast<double>(s.firstPlayer().dashDir.y), static_cast<int>(s.run.difficulty),
                  static_cast<int>(s.run.mode), s.run.bossesKilled, s.run.bossSpawned ? 1 : 0, static_cast<double>(s.run.bossHpFraction),
                  static_cast<unsigned long long>(sim::stateHash(s)));
    return json;
}

// What the screens look like right now, for the browser tests: which screen, the
// settings, the best run, and the position of every button in drawing buffer pixels.
const char* queryUi() {
    static char json[8192];
    const Settings& st = save::current().settings;
    const SaveData& saved = save::current();
    const BestRun& best = saved.bestFor(saved.mode, saved.difficulty);  // For the level and mode the title has selected.
    const char* devices[] = {"keyboard", "gamepad", "touch", "mouse"};
    const platform::SafeInsets insets = platform::safeInsets();
    int n = std::snprintf(json, sizeof(json),
                          "{\"screen\":\"%s\",\"demo\":%d,\"device\":\"%s\",\"width\":%d,\"height\":%d,\"unit\":%.3f,"
                          "\"music\":%.3f,\"effects\":%.3f,\"shake\":%.3f,\"flash\":%.3f,\"damageNumbers\":%d,\"reducedMotion\":%d,"
                          "\"bestTime\":%.1f,\"bestKills\":%d,\"bestLevel\":%d,\"wins\":%d,\"runs\":%d,"
                          "\"joystick\":%d,\"won\":%d,\"newBest\":%d,\"moveTime\":%.2f,\"safe\":[%.1f,%.1f,%.1f,%.1f],"
                          "\"safeRect\":[%.1f,%.1f,%.1f,%.1f],\"selected\":\"%s\",\"difficulty\":%d,\"map\":%d,\"summaryDifficulty\":%d,"
                          "\"audio\":%d,\"duck\":%.3f,\"fontBytes\":%d,\"mode\":%d,\"summaryMode\":%d,\"summaryBosses\":%d,"
                          "\"summaryTime\":%.1f,\"newBestTime\":%d,\"newBestKills\":%d,\"newBestLevel\":%d,"
                          "\"endlessBests\":[[%.1f,%d,%d,%d,%d],[%.1f,%d,%d,%d,%d],[%.1f,%d,%d,%d,%d],[%.1f,%d,%d,%d,%d]],"
                          "\"bests\":[[%.1f,%d,%d,%d,%d],[%.1f,%d,%d,%d,%d],[%.1f,%d,%d,%d,%d],[%.1f,%d,%d,%d,%d]],\"buttons\":[",
                          screenName(), g_inDemo ? 1 : 0, devices[static_cast<int>(g_input.lastDevice)], GetScreenWidth(), GetScreenHeight(),
                          static_cast<double>(ui::u()), static_cast<double>(st.musicVolume), static_cast<double>(st.effectsVolume),
                          static_cast<double>(st.screenShake), static_cast<double>(st.flashIntensity), st.damageNumbers ? 1 : 0,
                          st.reducedMotion ? 1 : 0, static_cast<double>(best.bestTimeSeconds), best.mostKills, best.highestLevel, best.wins,
                          best.runs, g_input.joystickActive ? 1 : 0, g_summary.won ? 1 : 0,
                          (g_summary.newBestTime || g_summary.newBestKills || g_summary.newBestLevel) ? 1 : 0, static_cast<double>(g_moveTime),
                          static_cast<double>(insets.left), static_cast<double>(insets.top), static_cast<double>(insets.right),
                          static_cast<double>(insets.bottom), static_cast<double>(ui::layout().left), static_cast<double>(ui::layout().top),
                          static_cast<double>(ui::layout().right), static_cast<double>(ui::layout().bottom), ui::selectedName(),
                          static_cast<int>(saved.difficulty), saved.map, static_cast<int>(g_summary.difficulty),
                          audio::isRunning() ? 1 : 0, static_cast<double>(audio::duckLevel()), font::memoryBytes(),
                          static_cast<int>(saved.mode), static_cast<int>(g_summary.mode), g_summary.bossesKilled,
                          static_cast<double>(g_summary.timeSeconds), g_summary.newBestTime ? 1 : 0, g_summary.newBestKills ? 1 : 0,
                          g_summary.newBestLevel ? 1 : 0,
                          static_cast<double>(saved.endlessBest[0].bestTimeSeconds), saved.endlessBest[0].mostKills, saved.endlessBest[0].highestLevel, saved.endlessBest[0].mostBosses, saved.endlessBest[0].runs,
                          static_cast<double>(saved.endlessBest[1].bestTimeSeconds), saved.endlessBest[1].mostKills, saved.endlessBest[1].highestLevel, saved.endlessBest[1].mostBosses, saved.endlessBest[1].runs,
                          static_cast<double>(saved.endlessBest[2].bestTimeSeconds), saved.endlessBest[2].mostKills, saved.endlessBest[2].highestLevel, saved.endlessBest[2].mostBosses, saved.endlessBest[2].runs,
                          static_cast<double>(saved.endlessBest[3].bestTimeSeconds), saved.endlessBest[3].mostKills, saved.endlessBest[3].highestLevel, saved.endlessBest[3].mostBosses, saved.endlessBest[3].runs,
                          static_cast<double>(saved.best[0].bestTimeSeconds), saved.best[0].mostKills, saved.best[0].highestLevel, saved.best[0].wins, saved.best[0].runs,
                          static_cast<double>(saved.best[1].bestTimeSeconds), saved.best[1].mostKills, saved.best[1].highestLevel, saved.best[1].wins, saved.best[1].runs,
                          static_cast<double>(saved.best[2].bestTimeSeconds), saved.best[2].mostKills, saved.best[2].highestLevel, saved.best[2].wins, saved.best[2].runs,
                          static_cast<double>(saved.best[3].bestTimeSeconds), saved.best[3].mostKills, saved.best[3].highestLevel, saved.best[3].wins, saved.best[3].runs);
    for (int i = 0; i < ui::registeredCount() && n < static_cast<int>(sizeof(json)) - 160; ++i) {
        const ui::Registered& r = ui::registered(i);
        n += std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "%s{\"n\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f}",
                           i > 0 ? "," : "", r.name, static_cast<double>(r.rect.x), static_cast<double>(r.rect.y),
                           static_cast<double>(r.rect.width), static_cast<double>(r.rect.height));
    }
    n += std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "],\"extents\":[");
    for (int i = 0; i < ui::extentCount() && n < static_cast<int>(sizeof(json)) - 160; ++i) {
        const ui::Registered& r = ui::extent(i);
        n += std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "%s{\"n\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f}",
                           i > 0 ? "," : "", r.name, static_cast<double>(r.rect.x), static_cast<double>(r.rect.y),
                           static_cast<double>(r.rect.width), static_cast<double>(r.rect.height));
    }
    std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "]}");
    return json;
}

// What co-op looks like on this machine, for the browser tests: the players and zombies as
// drawn here, and the network numbers.
const char* queryCoop() {
    static char json[2048];
    const GameState& s = *g_state;
    const netgame::Stats& st = netgame::stats();
    const char* role = coopHost() ? "host" : (coopGuest() ? "guest" : "none");
    int n = std::snprintf(json, sizeof(json),
                          "{\"role\":\"%s\",\"screen\":\"%s\",\"demo\":%d,\"run\":%d,\"slot\":%d,\"count\":%d,\"phase\":%d,\"tick\":%u,\"enemies\":%d,"
                          "\"kills\":%d,\"hostAway\":%d,\"hostPaused\":%d,\"ended\":%d,\"reconnecting\":%d,\"overlay\":%d,\"notice\":\"%s\",\"transport\":\"%s\",\"ping\":%d,"
                          "\"snapOut\":%d,\"snapIn\":%d,\"msgOut\":%d,\"msgIn\":%d,\"bytesOut\":%d,\"bytesIn\":%d,\"snapBytes\":%d,\"snapAvg\":%d,\"snapMax\":%d,"
                          "\"inputsIn\":%d,\"corrections\":%d,\"jumps\":%d,\"near\":[%d,%d,%d,%d],\"shieldText\":\"%s\",\"runMap\":%d,\"runDifficulty\":%d,\"runMode\":%d,\"players\":[",
                          role, screenName(), g_inDemo ? 1 : 0, netgame::settings().runNumber, s.localSlot, s.playerCount(), static_cast<int>(s.run.phase), s.run.tick,
                          s.enemies.count, s.run.kills, coopGuest() && netgame::clientHostAway() ? 1 : 0, coopGuest() && netgame::clientHostPaused() ? 1 : 0,
                          coopGuest() && netgame::clientRunEnded() ? 1 : 0, net::lobby().phase == net::LobbyPhase::Reconnecting ? 1 : 0, g_netOverlay ? 1 : 0,
                          ui::coopHudLastNotice(), coopRun() ? coopTransport() : "none",
                          coopRun() ? coopPing() : -1, st.snapshotsOutPerSec, st.snapshotsInPerSec, st.messagesOutPerSec, st.messagesInPerSec, st.bytesOutPerSec,
                          st.bytesInPerSec, st.lastSnapshotBytes, st.avgSnapshotBytes, st.maxSnapshotBytes, st.inputsInPerSec, st.corrections, st.snaps,
                          netgame::hostNearEnemies(0), netgame::hostNearEnemies(1), netgame::hostNearEnemies(2), netgame::hostNearEnemies(3), ui::levelUpShieldText(),
                          static_cast<int>(s.run.map), static_cast<int>(s.run.difficulty), static_cast<int>(s.run.mode));
    for (int slot = 0; slot < s.playerCount() && n > 0 && n < static_cast<int>(sizeof(json)) - 160; ++slot) {
        const Player& p = s.players[slot];
        n += std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n),
                           "%s{\"x\":%.2f,\"y\":%.2f,\"hp\":%.1f,\"downed\":%d,\"cards\":%d,\"level\":%d,\"xp\":%d,\"weapons\":%d,\"connected\":%d,\"shield\":%.2f}",
                           slot > 0 ? "," : "", static_cast<double>(p.pos.x), static_cast<double>(p.pos.y), static_cast<double>(p.hp), p.downed ? 1 : 0,
                           p.cardsOpen ? 1 : 0, p.level, p.xp, p.weaponCount, g_slotConnected[slot] ? 1 : 0, static_cast<double>(p.cardShield));
    }
    std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "]}");
    return json;
}

// The page's tab was hidden or shown. The host's run pauses for everyone at once, because a
// hidden tab may not draw another frame, and the others are told the host is away.
void pageHidden(bool hidden) {
    if (!coopHost() || g_inDemo) return;
    g_hostAway = hidden;
    if (hidden) pauseGame();
    netgame::hostSendPause(g_screen != Screen::Playing || hidden, hidden);
}

void debugCommand(int command, int value) {
    if (!g_debug) return;
    // Commands 100 and up drive the fake co-op server. They have no use for the game state.
    if (command >= net::kDebugReset) {
        net::debugCommand(command, value);
        return;
    }
    GameState& s = *g_state;
    // Any other debug command hands control back to the simulation, except the ones
    // that only move the camera or the clock, which also work while it is held still.
    const bool keepsShowcase = command == kSetTime || command == kMovePlayerX || command == kMovePlayerY || command == kSetQuality || command == kSetShake ||
                               command == kSetFlash || command == kSetDamageNumbers || command == kSetReducedMotion || command == kPushEvent || command == kResetFxCounters || command == kSetMap || command == kShowCarBoxes || command == kCarCover || command == kCrushNearestCar;
    if (!keepsShowcase) g_showcase = command == kShowcase;
    switch (command) {
        case kShowcase: worldview::setupShowcase(s, value); break;
        case kMovePlayerX: s.firstPlayer().pos.x = s.firstPlayer().prevPos.x = static_cast<float>(value); break;
        case kMovePlayerY: s.firstPlayer().pos.y = s.firstPlayer().prevPos.y = static_cast<float>(value); break;
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
            pushEvent(s, type, s.firstPlayer().pos, size);
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
        case kSetMap:
            save::current().map = static_cast<int>(maps::mapFromIndex(value));
            sim::setMap(s, selectedMap());
            break;
        case kStressEnemies: sim::setStressTargets(s, value, s.run.debugProjectileTarget); break;
        case kStressProjectiles: sim::setStressTargets(s, s.run.debugEnemyTarget, value); break;
        case kSetSeed:
            g_seedOverride = static_cast<uint64_t>(value);
            startRun(g_seedOverride);
            break;
        case kGrantXp: s.firstPlayer().xp = std::max(0, s.firstPlayer().xpToNext - value); break;
        case kStartRun: startRun(static_cast<uint64_t>(std::max(0, value))); break;
        case kForceDeath:
            if (!g_inDemo) {
                s.firstPlayer().hp = 0.0f;
                s.run.phase = RunPhase::Dead;
                pushEvent(s, EventType::PlayerDied, s.firstPlayer().pos);
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
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) weapons::grant(s, 0, static_cast<WeaponId>(i));
            for (int i = 0; i < s.firstPlayer().weaponCount; ++i) {
                WeaponSlot& slot = s.firstPlayer().weapons[i];
                slot.level = static_cast<uint8_t>(std::min(kMaxLevelPerItem, 1 + (i * 2) % 5 + (i == 0 ? 4 : 0)));
                weapons::refill(s.firstPlayer(), slot);
                slot.magazine = std::max(0, slot.magazine - i * 3);
            }
            if (s.firstPlayer().weaponCount > 0) s.firstPlayer().weapons[0].evolved = true;
            if (s.firstPlayer().weaponCount > 2) s.firstPlayer().weapons[2].reloadLeft = 0.5f;
            if (value == 1 && s.firstPlayer().weaponCount > 1) {
                s.firstPlayer().weapons[1].magazine = 0;
                s.firstPlayer().weapons[1].reserve = 0;
                s.firstPlayer().weapons[1].reloadLeft = 0.0f;
            }
            s.firstPlayer().passiveCount = 0;
            for (int i = 0; i < static_cast<int>(PassiveId::Count); ++i) {
                PassiveSlot& passive = s.firstPlayer().passives[s.firstPlayer().passiveCount++];
                passive.id = static_cast<PassiveId>(i);
                passive.level = static_cast<uint8_t>(1 + (i * 2) % 5);
            }
            upgrades::recomputeStats(s, 0);
            break;
        }
        case kSetHp: s.firstPlayer().hp = s.firstPlayer().maxHp * static_cast<float>(value) / 100.0f; break;
        case kSetTime: s.run.tick = static_cast<uint32_t>(std::max(0, value)) * 60u; break;
        case kSetKills: s.run.kills = value; break;
        case kSetLevel: s.firstPlayer().level = std::max(1, value); break;
        case kFakeDamage:
            for (int i = 0; i < static_cast<int>(WeaponId::Count); ++i) {
                s.run.damageByWeapon[i] = i < 7 ? static_cast<float>(value) * static_cast<float>((i * 7919) % 13 + 2) / 13.0f : 0.0f;
            }
            break;
        case kDash: g_pendingDash = true; break;
        case kHoldSim: g_holdSim = value != 0; break;
        case kShowCarBoxes: worldview::setShowCarBoxes(value != 0); break;
        case kCarCover: worldview::setCarCover(value != 0); break;
        case kCrushNearestCar: worldview::crushNearestCar(s); break;
        case kBossBesideCar: worldview::placeBossBesideCar(s); break;
        case kKillBoss:
            for (Enemy& enemy : s.enemies) {
                if (enemy.type == EnemyType::Boss) enemy.hp = 0.0f;
            }
            break;
        case kSetDashCooldown: s.firstPlayer().dashCooldown = balance::kDashCooldownSeconds * static_cast<float>(value) / 100.0f; break;
        case kLevelUp:
            if (!g_inDemo) upgrades::addXp(s, 0, std::max(1, s.firstPlayer().xpToNext - s.firstPlayer().xp));
            break;
        case kLevelUpSlot:
            if (!g_inDemo && value >= 0 && value < s.playerCount()) upgrades::addXp(s, value, std::max(1, s.players[value].xpToNext - s.players[value].xp));
            break;
        default: break;
    }
}

}  // namespace ls::app

#ifdef __EMSCRIPTEN__
extern "C" {
// Test hook: what the screens look like and where their buttons are.
EMSCRIPTEN_KEEPALIVE const char* ls_query_ui() { return ls::app::queryUi(); }
// Test hook: the co-op lobby and what the co-op screens hold.
EMSCRIPTEN_KEEPALIVE const char* ls_query_net() { return ls::ui::coopDebugJson(); }
// Test hook: co-op during a run, as this machine sees it.
EMSCRIPTEN_KEEPALIVE const char* ls_query_coop() { return ls::app::queryCoop(); }
// Called by the page when its tab is hidden (1) or shown again (0).
EMSCRIPTEN_KEEPALIVE void ls_net_page_hidden(int hidden) { ls::app::pageHidden(hidden != 0); }
// Test hook: counters that prove each effect and setting really changes behavior.
EMSCRIPTEN_KEEPALIVE const char* ls_query_effects() { return ls::effects::debugJson(); }
}
#endif
