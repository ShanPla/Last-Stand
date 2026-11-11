#pragma once

#include "core/GameState.h"
#include "data/Balance.h"

#include <cstdint>

// The co-op messages, as bytes. Every message is one small binary packet: a kind byte, a
// version byte, then fixed fields in little-endian order. Nothing here knows about the network,
// raylib or the clock, so unit tests can encode, decode and break these packets freely.
//
// Writing never grows a buffer: the caller gives a fixed array and its size. Reading checks
// every byte against the length it was given, so a short or broken packet is refused instead
// of being read past its end.
namespace ls::netgame {

inline constexpr uint8_t kProtocolVersion = 1;
// Big enough for any message. The relay allows 16 KB, and snapshots stop well below that.
inline constexpr int kMaxMessageBytes = 9000;

enum class MessageKind : uint8_t {
    RunStart = 1,  // Host to each player, reliable: the run's settings and who is who.
    Snapshot = 2,  // Host to each player, unreliable: the world around that player.
    Input = 3,     // Player to host, unreliable: the latest steps of movement and dash.
    CardPick = 4,  // Player to host, reliable: a level-up card or a reroll.
    Crushed = 5,   // Host to each player, reliable: cars the boss has flattened.
    Pause = 6,     // Host to each player, reliable: the host paused, or left the tab.
    RunEnd = 7,    // Host to each player, reliable: the run is over, with the results.
};

// Reads the kind of a packet, or 0 when it is too short or from another protocol version.
uint8_t kindOf(const uint8_t* data, int size);

// ---- Run start -------------------------------------------------------------------

struct RunStart {
    uint8_t runNumber = 0;  // Goes up with every new run in the room, so old packets can be told apart.
    uint64_t seed = 0;
    maps::MapId map = maps::kDefaultMap;
    Difficulty difficulty = Difficulty::Normal;
    RunMode mode = RunMode::Story;
    int playerCount = 0;
    int yourSlot = 0;                          // The player slot of whoever receives this.
    uint8_t netIds[kMaxPlayers] = {255, 255, 255, 255};  // The lobby slot of each player slot.
};
int encodeRunStart(const RunStart& message, uint8_t* out, int capacity);
bool decodeRunStart(const uint8_t* data, int size, RunStart& out);

// ---- Input -----------------------------------------------------------------------

// One step of one player's input, as sent. Movement is rounded to 1/127 of full speed.
struct InputSample {
    int8_t moveX = 0;
    int8_t moveY = 0;
    bool dash = false;
};

struct InputMessage {
    uint8_t runNumber = 0;
    uint32_t newestSeq = 0;  // The player's own step number for samples[0].
    int count = 0;           // samples[i] is the step newestSeq - i.
    InputSample samples[balance::kNetInputRepeat];
};
int encodeInput(const InputMessage& message, uint8_t* out, int capacity);
bool decodeInput(const uint8_t* data, int size, InputMessage& out);
InputSample quantizeInput(float moveX, float moveY, bool dash);
float unquantizeMove(int8_t value);

// ---- Card pick -------------------------------------------------------------------

struct CardPick {
    uint8_t runNumber = 0;
    int8_t card = -1;
    bool reroll = false;
};
int encodeCardPick(const CardPick& message, uint8_t* out, int capacity);
bool decodeCardPick(const uint8_t* data, int size, CardPick& out);

// ---- Crushed cars ----------------------------------------------------------------

inline constexpr int kMaxCrushedPerMessage = 64;
struct CrushedMessage {
    uint8_t runNumber = 0;
    int firstIndex = 0;  // Where these go in the run's list, so a repeat is never added twice.
    int count = 0;
    maps::CarId cars[kMaxCrushedPerMessage];
};
int encodeCrushed(const CrushedMessage& message, uint8_t* out, int capacity);
bool decodeCrushed(const uint8_t* data, int size, CrushedMessage& out);

// ---- Pause -----------------------------------------------------------------------

struct PauseMessage {
    uint8_t runNumber = 0;
    bool paused = false;  // The host has the pause menu open.
    bool away = false;    // The host's tab is hidden, so the host's game is not running.
};
int encodePause(const PauseMessage& message, uint8_t* out, int capacity);
bool decodePause(const uint8_t* data, int size, PauseMessage& out);

// ---- Run end ---------------------------------------------------------------------

struct RunEnd {
    uint8_t runNumber = 0;
    RunPhase phase = RunPhase::Dead;
    uint32_t tick = 0;
    int kills = 0;
    int bossesKilled = 0;
    int playerCount = 0;
    int levels[kMaxPlayers] = {};
    float damageByWeapon[kMaxPlayers][static_cast<int>(WeaponId::Count)] = {};
};
int encodeRunEnd(const RunEnd& message, uint8_t* out, int capacity);
bool decodeRunEnd(const uint8_t* data, int size, RunEnd& out);

// ---- Snapshot --------------------------------------------------------------------

// Positions are sent relative to origin (the receiving player), in eighths of a world unit.
inline constexpr float kPositionScale = 8.0f;
inline constexpr float kMaxOffset = 32767.0f / kPositionScale;  // About 4,000 units either way.

struct SnapshotHeader {
    uint8_t runNumber = 0;
    uint32_t tick = 0;
    RunPhase phase = RunPhase::Playing;
    bool bossSpawned = false;
    bool hostPaused = false;
    bool hostAway = false;
    int kills = 0;
    uint8_t bossPhase = 0;
    float bossHpFraction = 0.0f;
    int bossesKilled = 0;
    uint32_t ackSeq = 0;     // The newest input step of this player that the host has used.
    int crushedCount = 0;    // How many cars are flat, so a player can tell one is missing.
    Vec2 origin;
    int playerCount = 0;
};

// The world as one player should see it. The host fills one per player and the player decodes
// into one. The game's own structs are used, but only the fields that drawing needs are sent.
struct Snapshot {
    SnapshotHeader header;
    Player players[kMaxPlayers];
    Pickup pickups[kMaxPickups];
    int pickupCount = 0;
    Zone zones[balance::kNetMaxZones];
    int zoneCount = 0;
    SimEvent events[balance::kNetMaxEvents];
    uint8_t eventAge[balance::kNetMaxEvents] = {};  // Steps before header.tick that each event happened.
    int eventCount = 0;
    EnemyBullet enemyBullets[balance::kNetMaxEnemyBullets];
    int enemyBulletCount = 0;
    Enemy enemies[balance::kNetMaxEnemies];
    int enemyCount = 0;
    Projectile projectiles[balance::kNetMaxProjectiles];
    int projectileCount = 0;
    Gem gems[balance::kNetMaxGems];
    int gemCount = 0;
};

// Writes as much of the snapshot as fits in capacity, most important first: players, pickups,
// zones, events, enemy bullets, zombies, shots, then gems. Each list is cut where the space ends.
// Returns the bytes written, or 0 if even the players do not fit.
int encodeSnapshot(const Snapshot& snapshot, uint8_t* out, int capacity);
// Refuses (returns false) a packet that is short, too long for its lists, or of another kind.
bool decodeSnapshot(const uint8_t* data, int size, Snapshot& out);

// Enemy ids are sent as their low 16 bits. Two zombies alive at once would need 65,536 spawns
// between them to share one, and a clash only blends one zombie's movement for a frame.
inline uint16_t shortId(uint32_t id) { return static_cast<uint16_t>(id & 0xFFFFu); }

}  // namespace ls::netgame
