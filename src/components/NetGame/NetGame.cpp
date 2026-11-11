#include "components/NetGame/NetGame.h"

#include "components/Net/Net.h"
#include "components/Obstacles/Obstacles.h"
#include "components/Player/Player.h"
#include "data/Balance.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ls::netgame {

namespace {

// ---- Shared ----------------------------------------------------------------------

// Counts something over one second at a time, for the overlay's "per second" numbers.
struct RateCounter {
    int counting = 0;
    int lastSecond = 0;
    void add(int amount = 1) { counting += amount; }
    void roll() {
        lastSecond = counting;
        counting = 0;
    }
};

class BrowserLink final : public Link {
public:
    bool send(int toNetId, const uint8_t* data, int size, bool reliable) override { return net::sendGame(toNetId, data, size, reliable); }
    int receive(uint8_t* buffer, int capacity, int* fromNetId) override { return net::receiveGame(buffer, capacity, fromNetId); }
};

BrowserLink g_browserLink;

// ---- Host ------------------------------------------------------------------------

constexpr int kQueueCapacity = 32;

// What the host keeps for each other player.
struct Remote {
    bool inRun = false;
    bool connected = true;
    bool answered = false;     // An input for this run has arrived, so the settings got there.
    float startResend = 0.0f;
    // Input steps waiting to be used, oldest first, one per host step.
    InputSample queue[kQueueCapacity];
    uint32_t queueSeq[kQueueCapacity] = {};
    int queueHead = 0;
    int queueCount = 0;
    bool anyQueued = false;
    uint32_t newestQueued = 0;
    uint32_t ackSeq = 0;       // The newest step that was used. Sent back so they can check their prediction.
    InputSample last;
    int heldSteps = 0;
    bool hasPick = false;
    CardPick pick;
    // Events since the last snapshot that this player should see or hear.
    SimEvent events[balance::kNetMaxEvents];
    uint32_t eventTick[balance::kNetMaxEvents] = {};
    int eventCount = 0;
    int sentCrushed = 0;
    int clientCrushed = 0;     // Flat cars the player says it has, from its inputs.
    float crushedLag = 0.0f;   // How long the player has been behind what was sent.
    bool carryDash = false;    // A dropped step had a dash, so the next step gets it.
    int nearEnemies = 0;
};

Snapshot g_hostSnapshot;  // Built for one player at a time, then encoded.

struct Near {
    float distSq;
    uint16_t index;
};
Near g_near[kMaxProjectiles > kMaxEnemies ? kMaxProjectiles : kMaxEnemies];

constexpr int kStoredSnapshots = 4;
constexpr int kIdTableSize = 2048;  // A power of two, at least twice kNetMaxEnemies.
static_assert(kIdTableSize >= 2 * balance::kNetMaxEnemies, "the id table must stay at most half full");
constexpr int kHistorySize = 256;   // Prediction steps kept, about 4 seconds.
constexpr int kEventQueueSize = 256;

struct Stored {
    bool used = false;
    Snapshot snapshot;
    // Zombie id to its place in the snapshot, so a zombie can be found in the snapshot before.
    uint16_t keys[kIdTableSize] = {};
    int16_t slots[kIdTableSize] = {};
};

struct HistoryStep {
    uint32_t seq = 0;
    InputSample input;
    Vec2 pos;  // Where the prediction put this player after the step.
};

struct QueuedEvent {
    SimEvent event;
    uint32_t tick = 0;
};

}  // namespace

// Everything one machine knows about its co-op run. It is one struct so that a unit test can
// hold a host and a client side by side in one program and switch between them.
struct Session {
    Link* link = nullptr;
    Role role = Role::None;
    RunStart settings;
    Stats stats;
    float secondClock = 0.0f;
    RateCounter snapOut, snapIn, msgOut, msgIn, bytesOut, bytesIn, inputsIn;
    long long snapBytesTotal = 0;
    int snapBytesCount = 0;
    Remote remotes[kMaxPlayers];
    float snapshotClock = 0.0f;
    bool sentPaused = false;
    bool sentAway = false;
    float endResend = 0.0f;   // Seconds until the run's end is sent again.
    Stored stored[kStoredSnapshots];
    bool pendingStart = false;
    bool haveRun = false;
    uint32_t newestTick = 0;
    bool haveSnapshot = false;
    double renderTick = 0.0;
    float sinceSnapshot = 0.0f;
    float predictClock = 0.0f;
    float inputClock = 0.0f;
    uint32_t seq = 0;
    HistoryStep history[kHistorySize];
    Vec2 offset;        // Drawn position minus predicted position, shrinking to nothing.
    bool offsetApplied = false;
    bool dashWaiting = false;
    bool hostPaused = false;
    bool hostAwaySaid = false;
    bool runEnded = false;
    RunEnd runEnd;
    QueuedEvent eventQueue[kEventQueueSize];
    int eventHead = 0;
    int eventCount = 0;
    bool pickSent = false;
    CardPick lastPick;         // Sent again if the host's snapshots still show the same cards.
    float pickAge = 0.0f;
    uint32_t lastDashSeq = 0;  // The prediction step of this player's latest dash press.
    int crushedKnown = 0;      // Flat cars received, reported back in every input.
};

namespace {

Session g_defaultSession;
Session* g_session = &g_defaultSession;
Session& S() { return *g_session; }

Link& link() { return S().link ? *S().link : g_browserLink; }

uint8_t g_buffer[kMaxMessageBytes];

bool sendTo(int netId, const uint8_t* data, int size, bool reliable) {
    if (size <= 0) return false;
    S().msgOut.add();
    S().bytesOut.add(size);
    return link().send(netId, data, size, reliable);
}

// Once a second the counters become the numbers the overlay shows.
void tickStats(float dt) {
    S().secondClock += dt;
    if (S().secondClock < 1.0f) return;
    S().secondClock -= 1.0f;
    if (S().secondClock > 1.0f) S().secondClock = 0.0f;
    for (RateCounter* counter : {&S().snapOut, &S().snapIn, &S().msgOut, &S().msgIn, &S().bytesOut, &S().bytesIn, &S().inputsIn}) counter->roll();
    S().stats.snapshotsOutPerSec = S().snapOut.lastSecond;
    S().stats.snapshotsInPerSec = S().snapIn.lastSecond;
    S().stats.messagesOutPerSec = S().msgOut.lastSecond;
    S().stats.messagesInPerSec = S().msgIn.lastSecond;
    S().stats.bytesOutPerSec = S().bytesOut.lastSecond;
    S().stats.bytesInPerSec = S().bytesIn.lastSecond;
    S().stats.inputsInPerSec = S().inputsIn.lastSecond;
}

void noteSnapshotBytes(int size) {
    S().stats.lastSnapshotBytes = size;
    S().stats.maxSnapshotBytes = std::max(S().stats.maxSnapshotBytes, size);
    S().snapBytesTotal += size;
    S().snapBytesCount++;
    S().stats.avgSnapshotBytes = static_cast<int>(S().snapBytesTotal / S().snapBytesCount);
}

void resetStats() {
    S().stats = Stats{};
    S().secondClock = 0.0f;
    for (RateCounter* counter : {&S().snapOut, &S().snapIn, &S().msgOut, &S().msgIn, &S().bytesOut, &S().bytesIn, &S().inputsIn}) *counter = RateCounter{};
    S().snapBytesTotal = 0;
    S().snapBytesCount = 0;
}

void resetRemote(Remote& remote) { remote = Remote{}; }

void sendStart(int slot) {
    RunStart copy = S().settings;
    copy.yourSlot = slot;
    const int size = encodeRunStart(copy, g_buffer, sizeof(g_buffer));
    sendTo(S().settings.netIds[slot], g_buffer, size, true);
}

void beginHostRun(GameState& state) {
    sim::newRun(state, S().settings.seed, S().settings.difficulty, S().settings.map, S().settings.mode, S().settings.playerCount);
    state.localSlot = 0;
    S().snapshotClock = 0.0f;
    S().sentPaused = S().sentAway = false;
    S().endResend = 0.0f;
    // Every run is made from the room as it is now, so everyone in it is connected.
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        resetRemote(S().remotes[slot]);
        S().remotes[slot].inRun = slot > 0 && slot < S().settings.playerCount;
    }
    for (int slot = 1; slot < S().settings.playerCount; ++slot) sendStart(slot);
}

// Drops the oldest waiting step. Its dash moves on to the next step, so a press is never lost.
void dropOldest(Remote& remote) {
    const bool dash = remote.queue[remote.queueHead].dash;
    remote.queueHead = (remote.queueHead + 1) % kQueueCapacity;
    remote.queueCount--;
    if (!dash) return;
    if (remote.queueCount > 0) remote.queue[remote.queueHead].dash = true;
    else remote.carryDash = true;
}

void pushQueued(Remote& remote, uint32_t seq, const InputSample& sample) {
    if (remote.queueCount == kQueueCapacity) dropOldest(remote);
    const int at = (remote.queueHead + remote.queueCount) % kQueueCapacity;
    remote.queue[at] = sample;
    if (remote.carryDash) {
        remote.queue[at].dash = true;
        remote.carryDash = false;
    }
    remote.queueSeq[at] = seq;
    remote.queueCount++;
    remote.newestQueued = seq;
    remote.anyQueued = true;
}

void onInput(Remote& remote, const InputMessage& message) {
    if (message.newestSeq < static_cast<uint32_t>(message.count)) return;  // Steps start at 1.
    remote.answered = true;
    remote.clientCrushed = message.crushedCount;
    S().inputsIn.add();
    // Oldest first. Steps already queued (the repeats) are skipped.
    for (int i = message.count - 1; i >= 0; --i) {
        const uint32_t seq = message.newestSeq - static_cast<uint32_t>(i);
        if (remote.anyQueued && seq <= remote.newestQueued) continue;
        pushQueued(remote, seq, message.samples[i]);
    }
    // A player whose inputs pile up (a burst after a stall) is brought back to the present:
    // old steps are dropped, so their controls do not lag behind for the rest of the run.
    while (remote.queueCount > balance::kNetMaxQueuedInputs) dropOldest(remote);
}

// Whether a player should be told about an event. Their own events always, the big moments
// of the run always, and anything else only when it is near enough to be seen or heard.
bool eventFor(const GameState& state, int slot, const SimEvent& event) {
    switch (event.type) {
        case EventType::ZombieUnstuck: return false;
        case EventType::BossSpawned:
        case EventType::BossPhaseChanged:
        case EventType::BossKilled:
        case EventType::PlayerDied:
        case EventType::PlayerDowned:
        case EventType::PlayerRevived: return true;
        // Their own dash is already shown by their prediction.
        case EventType::PlayerDashed: return event.player != slot && distanceSq(event.pos, state.players[slot].pos) < balance::kNetViewRadius * balance::kNetViewRadius;
        default: break;
    }
    if (event.player == slot) return true;
    if (event.player != kNoPlayer && event.type != EventType::WeaponFired && event.type != EventType::Explosion) return false;
    return distanceSq(event.pos, state.players[slot].pos) < balance::kNetViewRadius * balance::kNetViewRadius;
}

bool commonEvent(EventType type) {
    return type == EventType::EnemyHit || type == EventType::WeaponFired || type == EventType::GemCollected || type == EventType::EnemyKilled || type == EventType::Explosion;
}

void collectEvents(const GameState& state) {
    for (int slot = 1; slot < S().settings.playerCount; ++slot) {
        Remote& remote = S().remotes[slot];
        if (!remote.inRun || !remote.connected) continue;
        for (const SimEvent& event : state.events) {
            if (!eventFor(state, slot, event)) continue;
            // Frequent events stop a little early, so the rare ones always have room.
            const int limit = commonEvent(event.type) ? balance::kNetMaxEvents - 16 : balance::kNetMaxEvents;
            if (remote.eventCount >= limit) continue;
            remote.events[remote.eventCount] = event;
            remote.eventTick[remote.eventCount] = state.run.tick;
            remote.eventCount++;
        }
    }
}

// Picks the nearest items of one kind within the view radius, nearest first, and returns how many.
template <typename Pool, typename PosOf>
int nearest(const Pool& pool, Vec2 origin, int limit, PosOf posOf) {
    const float reachSq = balance::kNetViewRadius * balance::kNetViewRadius;
    int found = 0;
    for (int i = 0; i < pool.count; ++i) {
        const float d = distanceSq(posOf(pool.items[i]), origin);
        if (d <= reachSq) g_near[found++] = Near{d, static_cast<uint16_t>(i)};
    }
    const auto closer = [](const Near& a, const Near& b) { return a.distSq < b.distSq; };
    if (found > limit) {
        std::nth_element(g_near, g_near + limit, g_near + found, closer);
        found = limit;
    }
    std::sort(g_near, g_near + found, closer);
    return found;
}

void buildSnapshot(const GameState& state, int slot, bool paused, bool away) {
    Snapshot& s = g_hostSnapshot;
    Remote& remote = S().remotes[slot];
    const Vec2 origin = state.players[slot].pos;
    SnapshotHeader& h = s.header;
    h.runNumber = S().settings.runNumber;
    h.tick = state.run.tick;
    h.phase = state.run.phase;
    h.bossSpawned = state.run.bossSpawned;
    h.hostPaused = paused;
    h.hostAway = away;
    h.kills = state.run.kills;
    h.bossPhase = state.run.bossPhase;
    h.bossHpFraction = state.run.bossHpFraction;
    h.bossesKilled = state.run.bossesKilled;
    h.ackSeq = remote.ackSeq;
    h.crushedCount = state.run.crushedCount;
    h.origin = origin;
    h.playerCount = state.playerCount();
    for (int i = 0; i < state.playerCount(); ++i) s.players[i] = state.players[i];

    s.pickupCount = 0;
    for (const Pickup& pickup : state.pickups) s.pickups[s.pickupCount++] = pickup;

    // Boss warnings first, because missing one can kill. Then the nearest of the rest.
    s.zoneCount = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (const Zone& zone : state.zones) {
            if (zone.hostile != (pass == 0) || s.zoneCount >= balance::kNetMaxZones) continue;
            if (!zone.hostile && distanceSq(zone.a, origin) > balance::kNetViewRadius * balance::kNetViewRadius) continue;
            s.zones[s.zoneCount++] = zone;
        }
    }

    s.eventCount = remote.eventCount;
    for (int i = 0; i < remote.eventCount; ++i) {
        s.events[i] = remote.events[i];
        const uint32_t age = state.run.tick - remote.eventTick[i];
        s.eventAge[i] = static_cast<uint8_t>(age > 255u ? 255u : age);
    }
    remote.eventCount = 0;

    int n = nearest(state.enemyBullets, origin, balance::kNetMaxEnemyBullets, [](const EnemyBullet& b) { return b.pos; });
    s.enemyBulletCount = n;
    for (int i = 0; i < n; ++i) s.enemyBullets[i] = state.enemyBullets[g_near[i].index];

    // The boss is always sent, wherever it is, because its warnings reach far.
    s.enemyCount = 0;
    for (const Enemy& enemy : state.enemies) {
        if (enemy.type == EnemyType::Boss && s.enemyCount < balance::kNetMaxEnemies) s.enemies[s.enemyCount++] = enemy;
    }
    n = nearest(state.enemies, origin, balance::kNetMaxEnemies - s.enemyCount, [](const Enemy& e) { return e.pos; });
    remote.nearEnemies = 0;
    for (const Enemy& enemy : state.enemies) {
        if (distanceSq(enemy.pos, origin) <= balance::kNetViewRadius * balance::kNetViewRadius) remote.nearEnemies++;
    }
    for (int i = 0; i < n; ++i) {
        const Enemy& enemy = state.enemies[g_near[i].index];
        if (enemy.type != EnemyType::Boss) s.enemies[s.enemyCount++] = enemy;
    }

    n = nearest(state.projectiles, origin, balance::kNetMaxProjectiles, [](const Projectile& p) { return p.pos; });
    s.projectileCount = n;
    for (int i = 0; i < n; ++i) s.projectiles[i] = state.projectiles[g_near[i].index];

    n = nearest(state.gems, origin, balance::kNetMaxGems, [](const Gem& g) { return g.pos; });
    s.gemCount = n;
    for (int i = 0; i < n; ++i) s.gems[i] = state.gems[g_near[i].index];
}

void sendCrushed(const GameState& state, int slot) {
    Remote& remote = S().remotes[slot];
    while (remote.sentCrushed < state.run.crushedCount) {
        CrushedMessage message;
        message.runNumber = S().settings.runNumber;
        message.firstIndex = remote.sentCrushed;
        message.count = std::min(kMaxCrushedPerMessage, state.run.crushedCount - remote.sentCrushed);
        for (int i = 0; i < message.count; ++i) message.cars[i] = state.run.crushed[remote.sentCrushed + i];
        const int size = encodeCrushed(message, g_buffer, sizeof(g_buffer));
        if (!sendTo(S().settings.netIds[slot], g_buffer, size, true)) return;  // Tried again next frame.
        remote.sentCrushed += message.count;
    }
}

void sendRunEnd(const GameState& state) {
    RunEnd end;
    end.runNumber = S().settings.runNumber;
    end.phase = state.run.phase;
    end.tick = state.run.tick;
    end.kills = state.run.kills;
    end.bossesKilled = state.run.bossesKilled;
    end.playerCount = state.playerCount();
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        end.levels[slot] = state.players[slot].level;
        for (int w = 0; w < static_cast<int>(WeaponId::Count); ++w) end.damageByWeapon[slot][w] = state.players[slot].damageByWeapon[w];
    }
    const int size = encodeRunEnd(end, g_buffer, sizeof(g_buffer));
    for (int slot = 1; slot < S().settings.playerCount; ++slot) {
        if (S().remotes[slot].connected) sendTo(S().settings.netIds[slot], g_buffer, size, true);
    }
}

// ---- Client ----------------------------------------------------------------------

Snapshot g_decode;  // Each packet is read here first, so a broken one never touches a stored snapshot.

void indexEnemies(Stored& stored) {
    std::memset(stored.slots, -1, sizeof(stored.slots));
    for (int i = 0; i < stored.snapshot.enemyCount; ++i) {
        const uint16_t key = shortId(stored.snapshot.enemies[i].id);
        uint32_t at = (key * 40503u) & (kIdTableSize - 1);
        while (stored.slots[at] >= 0) at = (at + 1) & (kIdTableSize - 1);
        stored.keys[at] = key;
        stored.slots[at] = static_cast<int16_t>(i);
    }
}

const Enemy* findEnemy(const Stored& stored, uint16_t key) {
    uint32_t at = (key * 40503u) & (kIdTableSize - 1);
    for (int probes = 0; probes < kIdTableSize && stored.slots[at] >= 0; ++probes) {
        if (stored.keys[at] == key) return &stored.snapshot.enemies[stored.slots[at]];
        at = (at + 1) & (kIdTableSize - 1);
    }
    return nullptr;
}

void clearClient() {
    for (Stored& stored : S().stored) stored.used = false;
    S().haveSnapshot = false;
    S().newestTick = 0;
    S().renderTick = 0.0;
    S().sinceSnapshot = 0.0f;
    S().predictClock = 0.0f;
    S().inputClock = 0.0f;
    S().seq = 0;
    for (HistoryStep& step : S().history) step = HistoryStep{};
    S().offset = Vec2{};
    S().offsetApplied = false;
    S().dashWaiting = false;
    S().hostPaused = false;
    S().hostAwaySaid = false;
    S().runEnded = false;
    S().eventHead = 0;
    S().eventCount = 0;
    S().pickSent = false;
    S().lastPick = CardPick{};
    S().pickAge = 0.0f;
    S().lastDashSeq = 0;
    S().crushedKnown = 0;
}

Stored* newestStored() {
    Stored* best = nullptr;
    for (Stored& stored : S().stored) {
        if (stored.used && (!best || stored.snapshot.header.tick > best->snapshot.header.tick)) best = &stored;
    }
    return best;
}

void queueEvent(const SimEvent& event, uint32_t tick) {
    if (S().eventCount == kEventQueueSize) {  // Full: the oldest goes.
        S().eventHead = (S().eventHead + 1) % kEventQueueSize;
        S().eventCount--;
    }
    S().eventQueue[(S().eventHead + S().eventCount) % kEventQueueSize] = QueuedEvent{event, tick};
    S().eventCount++;
}

// The prediction check. The host says where this player was after the step it last used. If
// the prediction for that same step was somewhere else, every step since is moved by the
// difference. The drawn soldier is held where it was and slides over a few frames, unless the
// difference is so big (a revive, a long stall) that sliding would look worse than a jump.
void reconcile(GameState& state, const Snapshot& snapshot) {
    const SnapshotHeader& h = snapshot.header;
    const int local = state.localSlot;
    if (h.ackSeq == 0 || local >= h.playerCount) return;
    const HistoryStep& step = S().history[h.ackSeq % kHistorySize];
    if (step.seq != h.ackSeq) return;  // Too old to check.
    const Vec2 error = snapshot.players[local].pos - step.pos;
    const float size = length(error);
    if (size < 0.25f) return;
    Player& me = state.players[local];
    me.pos += error;
    me.prevPos += error;
    for (HistoryStep& later : S().history) {
        if (later.seq >= h.ackSeq) later.pos += error;
    }
    S().stats.predictionError = size;
    if (size > balance::kNetSnapDistance) {
        S().offset = Vec2{};
        S().stats.snaps++;
    } else {
        S().offset -= error;
        S().stats.corrections++;
    }
}

// The parts of this player that the host decides, copied over the prediction. Where they
// stand, which way they face and their dash stay predicted.
void takeHostFields(Player& me, const Player& host, uint32_t ackSeq) {
    const Vec2 pos = me.pos;
    const Vec2 prevPos = me.prevPos;
    const Vec2 facing = me.facing;
    const Vec2 dashDir = me.dashDir;
    const float dashLeft = me.dashLeft;
    const float dashCooldown = me.dashCooldown;
    const float dashInvulnerable = me.dashInvulnerable;
    me = host;
    me.pos = pos;
    me.prevPos = prevPos;
    me.facing = facing;
    me.dashDir = dashDir;
    me.dashLeft = dashLeft;
    me.dashInvulnerable = dashInvulnerable;
    // The cooldown is the host's once the host has used the step of the latest dash press, run
    // on by the steps since. Before that, a snapshot can show a cooldown from before the dash, and
    // taking it would let this player dash again while the host still refuses.
    if (ackSeq >= S().lastDashSeq) {
        me.dashCooldown = std::max(0.0f, host.dashCooldown - static_cast<float>(S().seq - ackSeq) * kDt);
    } else {
        me.dashCooldown = dashCooldown;
    }
}

void onSnapshot(GameState& state, const uint8_t* data, int size) {
    if (!decodeSnapshot(data, size, g_decode)) return;
    const SnapshotHeader& h = g_decode.header;
    if (h.runNumber != S().settings.runNumber || h.playerCount != S().settings.playerCount) return;
    if (S().haveSnapshot && h.tick < S().newestTick) return;  // Late, and a newer one is already here.
    S().snapIn.add();
    noteSnapshotBytes(size);
    // The same tick again (the host is paused) replaces the newest. A new tick replaces the oldest.
    Stored* target = nullptr;
    if (S().haveSnapshot && h.tick == S().newestTick) target = newestStored();
    if (!target) {
        for (Stored& stored : S().stored) {
            if (!stored.used) {
                target = &stored;
                break;
            }
            if (!target || stored.snapshot.header.tick < target->snapshot.header.tick) target = &stored;
        }
    }
    const bool firstOfTick = !(S().haveSnapshot && h.tick == S().newestTick);
    target->snapshot = g_decode;
    target->used = true;
    indexEnemies(*target);
    if (!S().haveSnapshot) S().renderTick = static_cast<double>(h.tick) - balance::kNetInterpDelaySteps;
    S().haveSnapshot = true;
    S().newestTick = h.tick;
    S().sinceSnapshot = 0.0f;
    S().hostPaused = h.hostPaused;
    S().hostAwaySaid = h.hostAway;

    if (firstOfTick) {
        for (int i = 0; i < g_decode.eventCount; ++i) queueEvent(g_decode.events[i], h.tick - g_decode.eventAge[i]);
    }
    Player& me = state.players[state.localSlot];
    reconcile(state, g_decode);
    takeHostFields(me, g_decode.players[state.localSlot], h.ackSeq);
    if (S().pickSent && (!me.cardsOpen || me.offerSerial != S().lastPick.offerSerial)) S().pickSent = false;
}

void onCrushed(GameState& state, const uint8_t* data, int size) {
    CrushedMessage message;
    if (!decodeCrushed(data, size, message) || message.runNumber != S().settings.runNumber) return;
    // A gap means an earlier message was lost. The host sends again from the count this player
    // reports, so this one can wait.
    if (message.firstIndex > state.run.crushedCount) return;
    bool added = false;
    for (int i = state.run.crushedCount - message.firstIndex; i < message.count; ++i) {
        if (state.run.crushedCount >= kMaxCrushedCars) break;
        state.run.crushed[state.run.crushedCount++] = message.cars[i];
        added = true;
    }
    S().crushedKnown = state.run.crushedCount;
    // A flat car is no longer solid, so the prediction must stop sliding around it.
    if (added) obstacles::invalidate(state);
}

void readClientMessages(GameState& state) {
    int from = -1;
    for (;;) {
        const int size = link().receive(g_buffer, sizeof(g_buffer), &from);
        if (size <= 0) break;
        // Only the host's messages count. A client never hears from other clients.
        if (from != S().settings.netIds[0] && S().haveRun) continue;
        S().msgIn.add();
        S().bytesIn.add(size);
        switch (static_cast<MessageKind>(kindOf(g_buffer, size))) {
            case MessageKind::RunStart: {
                RunStart start;
                if (!decodeRunStart(g_buffer, size, start)) break;
                if (S().haveRun && start.runNumber == S().settings.runNumber && start.seed == S().settings.seed) break;  // A repeat.
                S().settings = start;
                S().pendingStart = true;
                S().haveRun = true;
                break;
            }
            case MessageKind::Snapshot:
                if (S().haveRun && !S().pendingStart) onSnapshot(state, g_buffer, size);
                break;
            case MessageKind::Crushed:
                if (S().haveRun && !S().pendingStart) onCrushed(state, g_buffer, size);
                break;
            case MessageKind::Pause: {
                PauseMessage pause;
                if (decodePause(g_buffer, size, pause) && pause.runNumber == S().settings.runNumber) {
                    S().hostPaused = pause.paused;
                    S().hostAwaySaid = pause.away;
                }
                break;
            }
            case MessageKind::RunEnd: {
                RunEnd end;
                if (decodeRunEnd(g_buffer, size, end) && end.runNumber == S().settings.runNumber) {
                    S().runEnd = end;
                    S().runEnded = true;
                }
                break;
            }
            default: break;
        }
        // A new run stops the reading, so its snapshots wait until the state has been reset.
        if (S().pendingStart) break;
    }
}

void sendInputs() {
    if (S().seq == 0) return;
    InputMessage message;
    message.runNumber = S().settings.runNumber;
    message.newestSeq = S().seq;
    message.crushedCount = S().crushedKnown;
    message.count = static_cast<int>(std::min<uint32_t>(S().seq, balance::kNetInputRepeat));
    for (int i = 0; i < message.count; ++i) message.samples[i] = S().history[(S().seq - static_cast<uint32_t>(i)) % kHistorySize].input;
    const int size = encodeInput(message, g_buffer, sizeof(g_buffer));
    sendTo(S().settings.netIds[0], g_buffer, size, false);
}

// One prediction step: the same player::update the host will run for this input.
void predictStep(GameState& state, const InputSample& input, bool canMove) {
    S().seq++;
    HistoryStep& step = S().history[S().seq % kHistorySize];
    step.seq = S().seq;
    step.input = input;
    if (input.dash) S().lastDashSeq = S().seq;
    Player& me = state.players[state.localSlot];
    if (canMove) {
        obstacles::update(state);
        player::update(state, state.localSlot, unquantizeMove(input.moveX), unquantizeMove(input.moveY), input.dash);
    } else {
        me.prevPos = me.pos;
    }
    step.pos = me.pos;
}

// Where something with a velocity is at the drawing time, from where it was in a snapshot.
// It moves at most a tenth of a second either way, so a stalled stream never sends it far.
Vec2 carried(Vec2 pos, Vec2 vel, double seconds) {
    const float t = static_cast<float>(std::clamp(seconds, -0.1, 0.1));
    return pos + vel * t;
}

// Fills the state with the world at the drawing time, between the two snapshots around it.
void buildPicture(GameState& state) {
    Stored* newest = newestStored();
    if (!newest) return;
    // The two snapshots around the drawing time. Before the oldest, or after the newest, the
    // nearest one is used alone.
    Stored* before = nullptr;
    Stored* after = nullptr;
    for (Stored& stored : S().stored) {
        if (!stored.used) continue;
        const double tick = stored.snapshot.header.tick;
        if (tick <= S().renderTick && (!before || tick > before->snapshot.header.tick)) before = &stored;
        if (tick >= S().renderTick && (!after || tick < after->snapshot.header.tick)) after = &stored;
    }
    if (!before) before = after;
    if (!after) after = before;
    const Snapshot& a = before->snapshot;
    const Snapshot& b = after->snapshot;
    const double span = static_cast<double>(b.header.tick) - static_cast<double>(a.header.tick);
    const float t = span > 0.0 ? static_cast<float>(std::clamp((S().renderTick - a.header.tick) / span, 0.0, 1.0)) : 1.0f;
    const double sinceB = (S().renderTick - static_cast<double>(b.header.tick)) / 60.0;

    // The run's numbers come from the newest snapshot, so the HUD never waits for the blend.
    const SnapshotHeader& h = newest->snapshot.header;
    RunState& run = state.run;
    run.tick = static_cast<uint32_t>(std::max(0.0, S().renderTick));
    run.phase = h.phase == RunPhase::LevelUp ? RunPhase::Playing : h.phase;
    run.kills = h.kills;
    run.bossSpawned = h.bossSpawned;
    run.bossPhase = h.bossPhase;
    run.bossHpFraction = h.bossHpFraction;
    run.bossesKilled = h.bossesKilled;

    // Other players: blended between the two snapshots, with everything else from the newest.
    for (int slot = 0; slot < state.playerCount(); ++slot) {
        if (slot == state.localSlot) continue;
        Player& p = state.players[slot];
        p = newest->snapshot.players[slot];
        p.pos = lerp(a.players[slot].pos, b.players[slot].pos, t);
        p.prevPos = p.pos;
    }

    state.enemies.clear();
    for (int i = 0; i < b.enemyCount; ++i) {
        Enemy* enemy = state.enemies.add();
        if (!enemy) break;
        *enemy = b.enemies[i];
        if (const Enemy* old = findEnemy(*before, shortId(b.enemies[i].id)); old && before != after) enemy->pos = lerp(old->pos, b.enemies[i].pos, t);
        enemy->prevPos = enemy->pos;
    }
    state.projectiles.clear();
    for (int i = 0; i < b.projectileCount; ++i) {
        Projectile* shot = state.projectiles.add();
        if (!shot) break;
        *shot = b.projectiles[i];
        shot->pos = carried(shot->pos, shot->vel, sinceB);
        shot->prevPos = shot->pos;
    }
    state.enemyBullets.clear();
    for (int i = 0; i < b.enemyBulletCount; ++i) {
        EnemyBullet* bullet = state.enemyBullets.add();
        if (!bullet) break;
        *bullet = b.enemyBullets[i];
        bullet->pos = carried(bullet->pos, bullet->vel, sinceB);
        bullet->prevPos = bullet->pos;
    }
    state.zones.clear();
    for (int i = 0; i < b.zoneCount; ++i) {
        if (Zone* zone = state.zones.add()) *zone = b.zones[i];
    }
    state.gems.clear();
    for (int i = 0; i < b.gemCount; ++i) {
        if (Gem* gem = state.gems.add()) *gem = b.gems[i];
    }
    state.pickups.clear();
    for (int i = 0; i < b.pickupCount; ++i) {
        if (Pickup* pickup = state.pickups.add()) *pickup = b.pickups[i];
    }

    // Events wait until the drawing time reaches the step they happened on, so a zombie falls
    // at the moment it is drawn being hit, not before.
    while (S().eventCount > 0) {
        const QueuedEvent& queued = S().eventQueue[S().eventHead];
        if (static_cast<double>(queued.tick) > S().renderTick) break;
        if (state.events.count < kMaxEvents) {
            if (SimEvent* event = state.events.add()) *event = queued.event;
        }
        S().eventHead = (S().eventHead + 1) % kEventQueueSize;
        S().eventCount--;
    }
}

void applyOffset(GameState& state, Vec2 offset) {
    Player& me = state.players[state.localSlot];
    me.pos += offset;
    me.prevPos += offset;
}

}  // namespace

Link& browserLink() { return g_browserLink; }

// ---- For everyone --------------------------------------------------------------

Role role() { return S().role; }
const RunStart& settings() { return S().settings; }
const Stats& stats() { return S().stats; }

int netIdOf(int slot) {
    if (slot < 0 || slot >= kMaxPlayers || slot >= S().settings.playerCount) return -1;
    return S().settings.netIds[slot] == 255 ? -1 : S().settings.netIds[slot];
}

int slotOfNetId(int netId) {
    for (int slot = 0; slot < S().settings.playerCount && slot < kMaxPlayers; ++slot) {
        if (S().settings.netIds[slot] == netId) return slot;
    }
    return -1;
}

namespace {

void forgetSession() {
    S().role = Role::None;
    S().settings = RunStart{};
    for (Remote& remote : S().remotes) resetRemote(remote);
    clearClient();
    S().pendingStart = false;
    S().haveRun = false;
    resetStats();
}

}  // namespace

void stop() {
    forgetSession();
    // Messages still waiting belong to the room that was just left.
    int from = -1;
    while (link().receive(g_buffer, sizeof(g_buffer), &from) > 0) {
    }
}

void setLink(Link* newLink) { S().link = newLink; }

Session* createSession() { return new Session(); }
void destroySession(Session* session) {
    if (g_session == session) g_session = &g_defaultSession;
    delete session;
}
void useSession(Session* session) { g_session = session ? session : &g_defaultSession; }

// ---- Host ------------------------------------------------------------------------

void hostStart(GameState& state, const RunStart& start) {
    forgetSession();
    S().role = Role::Host;
    S().settings = start;
    S().settings.yourSlot = 0;
    for (Remote& remote : S().remotes) remote.connected = true;
    beginHostRun(state);
}

void hostRestart(GameState& state, const RunStart& start) {
    if (S().role != Role::Host) return;
    const uint8_t next = static_cast<uint8_t>(S().settings.runNumber + 1);
    S().settings = start;
    S().settings.runNumber = next;
    S().settings.yourSlot = 0;
    beginHostRun(state);
}

void hostReceive() {
    if (S().role != Role::Host) return;
    int from = -1;
    for (;;) {
        const int size = link().receive(g_buffer, sizeof(g_buffer), &from);
        if (size <= 0) break;
        S().msgIn.add();
        S().bytesIn.add(size);
        const int slot = slotOfNetId(from);
        if (slot <= 0) continue;
        Remote& remote = S().remotes[slot];
        switch (static_cast<MessageKind>(kindOf(g_buffer, size))) {
            case MessageKind::Input: {
                InputMessage message;
                if (decodeInput(g_buffer, size, message) && message.runNumber == S().settings.runNumber) onInput(remote, message);
                break;
            }
            case MessageKind::CardPick: {
                CardPick pick;
                if (decodeCardPick(g_buffer, size, pick) && pick.runNumber == S().settings.runNumber) {
                    remote.pick = pick;
                    remote.hasPick = true;
                }
                break;
            }
            default: break;
        }
    }
}

void hostFillInputs(const GameState& state, InputFrame inputs[], int count) {
    if (S().role != Role::Host) return;
    for (int slot = 1; slot < count && slot < S().settings.playerCount; ++slot) {
        Remote& remote = S().remotes[slot];
        InputFrame& input = inputs[slot];
        input = InputFrame{};
        if (!remote.connected) continue;
        if (remote.queueCount > 0) {
            const InputSample& sample = remote.queue[remote.queueHead];
            remote.ackSeq = remote.queueSeq[remote.queueHead];
            remote.queueHead = (remote.queueHead + 1) % kQueueCapacity;
            remote.queueCount--;
            remote.last = sample;
            remote.heldSteps = 0;
            input.moveX = unquantizeMove(sample.moveX);
            input.moveY = unquantizeMove(sample.moveY);
            input.dash = sample.dash;
        } else if (remote.heldSteps < balance::kNetHoldInputSteps) {
            // Late: keep going the way they were going for a moment. A dash is never repeated.
            remote.heldSteps++;
            input.moveX = unquantizeMove(remote.last.moveX);
            input.moveY = unquantizeMove(remote.last.moveY);
        }
        if (remote.carryDash && remote.queueCount == 0) {
            // A dropped step had a dash and nothing newer is waiting: it is used now.
            input.dash = true;
            remote.carryDash = false;
        }
        if (remote.hasPick) {
            // Only for the cards on the player's screen. A pick made for cards that have since
            // been replaced (a reroll, or the next level's cards) is dropped.
            const Player& player = state.players[slot];
            if (player.cardsOpen && remote.pick.offerSerial == player.offerSerial) {
                input.chooseCard = remote.pick.card;
                input.reroll = remote.pick.reroll;
            }
            remote.hasPick = false;
        }
    }
}

void hostSetConnected(int slot, bool connected) {
    if (slot > 0 && slot < kMaxPlayers) S().remotes[slot].connected = connected;
}

bool hostConnected(int slot) { return slot <= 0 || (slot < kMaxPlayers && S().remotes[slot].connected); }

void hostBeforeStep(GameState& state) {
    if (S().role != Role::Host) return;
    for (int slot = 1; slot < state.playerCount(); ++slot) {
        if (S().remotes[slot].connected) continue;
        // Gone for good: down, and never revived. The run goes on for the others, and ends
        // the usual way when everyone still playing is down too.
        Player& player = state.players[slot];
        if (!player.downed) player.hp = 0.0f;
        player.reviveProgress = 0.0f;
    }
}

void hostSendPause(bool paused, bool away) {
    if (S().role != Role::Host) return;
    S().sentPaused = paused;
    S().sentAway = away;
    PauseMessage message;
    message.runNumber = S().settings.runNumber;
    message.paused = paused;
    message.away = away;
    const int size = encodePause(message, g_buffer, sizeof(g_buffer));
    for (int slot = 1; slot < S().settings.playerCount; ++slot) {
        if (S().remotes[slot].connected) sendTo(S().settings.netIds[slot], g_buffer, size, true);
    }
}

void hostAfterFrame(GameState& state, float dt, bool paused, bool away) {
    if (S().role != Role::Host) return;
    tickStats(dt);
    collectEvents(state);
    if (paused != S().sentPaused || away != S().sentAway) hostSendPause(paused, away);

    // The settings again, for anyone who has not answered yet. A reliable message can still be
    // lost if it was sent before the direct connection was ready.
    for (int slot = 1; slot < S().settings.playerCount; ++slot) {
        Remote& remote = S().remotes[slot];
        if (remote.answered || !remote.connected) continue;
        remote.startResend += dt;
        if (remote.startResend >= balance::kNetStartResendSeconds) {
            remote.startResend = 0.0f;
            sendStart(slot);
        }
    }
    for (int slot = 1; slot < S().settings.playerCount; ++slot) {
        Remote& remote = S().remotes[slot];
        if (!remote.connected) continue;
        // A player still behind on flat cars a while after they were sent lost one: send again
        // from what they have. The wait leaves time for messages already on their way.
        if (remote.answered && remote.clientCrushed < remote.sentCrushed) {
            remote.crushedLag += dt;
            if (remote.crushedLag > balance::kNetResendSeconds) {
                remote.sentCrushed = remote.clientCrushed;
                remote.crushedLag = 0.0f;
            }
        } else {
            remote.crushedLag = 0.0f;
        }
        sendCrushed(state, slot);
    }

    S().snapshotClock += dt;
    const float interval = 1.0f / balance::kNetSnapshotsPerSecond;
    if (S().snapshotClock >= interval) {
        S().snapshotClock = std::fmod(S().snapshotClock, interval);
        for (int slot = 1; slot < S().settings.playerCount; ++slot) {
            if (!S().remotes[slot].connected) continue;
            buildSnapshot(state, slot, paused, away);
            const int size = encodeSnapshot(g_hostSnapshot, g_buffer, balance::kNetSnapshotBytes);
            if (size <= 0) continue;
            noteSnapshotBytes(size);
            S().snapOut.add();
            sendTo(S().settings.netIds[slot], g_buffer, size, false);
        }
    }

    // The run's end, again and again until the next run, so a lost copy costs at most a moment.
    if (state.run.phase == RunPhase::Won || state.run.phase == RunPhase::Dead) {
        S().endResend -= dt;
        if (S().endResend <= 0.0f) {
            S().endResend = balance::kNetResendSeconds;
            sendRunEnd(state);
        }
    }
}

int hostNearEnemies(int slot) { return slot > 0 && slot < kMaxPlayers ? S().remotes[slot].nearEnemies : 0; }

// ---- Client ----------------------------------------------------------------------

void clientListen() {
    // Nothing waiting is thrown away: the host's settings may already be here, since they are
    // sent the moment the host's game sees the room start.
    forgetSession();
    S().role = Role::Client;
}

bool clientNewRun(GameState& state) {
    if (S().role != Role::Client) return false;
    // Before the first run nothing else is going on, so the messages are read here. During a
    // run clientFrame reads them, with the drawn soldier's smoothing taken off first.
    if (!S().pendingStart && !S().haveRun) readClientMessages(state);
    if (!S().pendingStart) return false;
    S().pendingStart = false;
    clearClient();
    // The starting state is the same one the host made, from the same settings. From here on
    // it is only ever filled from snapshots, never stepped.
    sim::newRun(state, S().settings.seed, S().settings.difficulty, S().settings.map, S().settings.mode, S().settings.playerCount);
    state.localSlot = S().settings.yourSlot;
    // An input right away tells the host the settings arrived.
    S().history[0] = HistoryStep{};
    predictStep(state, InputSample{}, false);
    sendInputs();
    return true;
}

void clientFrame(GameState& state, float dt, float moveX, float moveY, bool dash) {
    if (S().role != Role::Client || !S().haveRun) return;
    tickStats(dt);
    if (S().offsetApplied) applyOffset(state, S().offset * -1.0f);
    S().offsetApplied = false;
    readClientMessages(state);
    if (S().pendingStart) return;  // The caller starts the new run with clientNewRun.

    S().sinceSnapshot += dt;
    S().stats.snapshotAge = S().sinceSnapshot;
    // A pick the host has not taken yet, while its snapshots still show the same cards, is sent
    // again: it may have been lost on the way.
    if (S().pickSent) {
        S().pickAge += dt;
        const Player& me = state.localPlayer();
        if (S().pickAge > balance::kNetResendSeconds && me.cardsOpen && me.offerSerial == S().lastPick.offerSerial) {
            S().pickAge = 0.0f;
            const int size = encodeCardPick(S().lastPick, g_buffer, sizeof(g_buffer));
            sendTo(S().settings.netIds[0], g_buffer, size, true);
        }
    }
    if (dash) S().dashWaiting = true;
    const bool frozen = S().hostPaused || clientHostAway() || state.run.phase == RunPhase::Dead || state.run.phase == RunPhase::Won || !S().haveSnapshot;

    // Prediction runs at the simulation's own 60 steps a second, like the host.
    S().predictClock += dt;
    int steps = 0;
    while (S().predictClock >= kDt && steps < 5) {
        S().predictClock -= kDt;
        steps++;
        const InputSample input = frozen ? InputSample{} : quantizeInput(moveX, moveY, S().dashWaiting);
        if (input.dash) S().dashWaiting = false;
        predictStep(state, input, !frozen);
    }
    if (steps == 5) S().predictClock = 0.0f;
    if (frozen) S().dashWaiting = false;

    S().inputClock += dt;
    if (S().inputClock >= 1.0f / balance::kNetInputsPerSecond) {
        S().inputClock = std::fmod(S().inputClock, 1.0f / balance::kNetInputsPerSecond);
        sendInputs();
    }

    // The drawing time follows the newest snapshot at a fixed delay. Small drifts are eased
    // out, big ones (a stall) are jumped. It never runs past the newest snapshot by much.
    if (S().haveSnapshot) {
        S().renderTick += static_cast<double>(dt) * 60.0;
        const double target = static_cast<double>(S().newestTick) - balance::kNetInterpDelaySteps;
        const double drift = target - S().renderTick;
        if (std::abs(drift) > 30.0) S().renderTick = target;
        else S().renderTick += drift * 0.05;
        S().renderTick = std::min(S().renderTick, static_cast<double>(S().newestTick) + 3.0);
        buildPicture(state);
    }

    // The drawn soldier eases toward the prediction.
    const float keep = std::pow(1.0f - balance::kNetCorrectionRate, std::max(0.0f, dt * 60.0f));
    S().offset *= keep;
    if (lengthSq(S().offset) < 0.01f) S().offset = Vec2{};
    applyOffset(state, S().offset);
    S().offsetApplied = true;
}

void clientPickCard(const GameState& state, int card, bool reroll) {
    if (S().role != Role::Client || (card < 0 && !reroll)) return;
    const Player& me = state.localPlayer();
    if (!me.cardsOpen) return;
    // One pick for one set of cards, named by the host's offer number. The snapshots show
    // whether the host took it, and clientFrame sends it again if not.
    if (S().pickSent && me.offerSerial == S().lastPick.offerSerial) return;
    CardPick pick;
    pick.runNumber = S().settings.runNumber;
    pick.offerSerial = me.offerSerial;
    pick.card = static_cast<int8_t>(card);
    pick.reroll = card < 0 && reroll;
    const int size = encodeCardPick(pick, g_buffer, sizeof(g_buffer));
    sendTo(S().settings.netIds[0], g_buffer, size, true);
    S().pickSent = true;
    S().lastPick = pick;
    S().pickAge = 0.0f;
}

float clientAlpha() { return std::clamp(S().predictClock / kDt, 0.0f, 1.0f); }

bool clientHostPaused() { return S().hostPaused; }
bool clientHostAway() { return S().hostAwaySaid || (S().haveSnapshot && S().sinceSnapshot > balance::kNetHostSilentSeconds); }
bool clientRunEnded() { return S().runEnded; }
const RunEnd& clientRunEnd() { return S().runEnd; }

}  // namespace ls::netgame
