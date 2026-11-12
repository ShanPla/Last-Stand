#include "doctest.h"

#include "components/Enemies/Enemies.h"
#include "components/NetGame/Codec/Codec.h"
#include "components/NetGame/NetGame.h"
#include "components/Upgrades/Upgrades.h"
#include "components/Weapons/Weapons.h"
#include "core/Rng.h"
#include "core/Sim.h"
#include "data/Balance.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

// Online co-op during the run: the message codec, and a host and a client joined by a pipe in
// memory, with the real simulation behind the host.

using namespace ls;
using namespace ls::netgame;

namespace {

std::unique_ptr<GameState> coopState(int players, uint64_t seed = 77) {
    auto state = std::make_unique<GameState>();
    sim::newRun(*state, seed, Difficulty::Normal, maps::MapId::City, RunMode::Story, players);
    return state;
}

// Runs a co-op state for a while with everyone walking in circles, so the world has zombies,
// shots, gems and events in it.
void play(GameState& state, int steps) {
    InputFrame inputs[kMaxPlayers];
    for (int i = 0; i < steps; ++i) {
        for (int slot = 0; slot < state.playerCount(); ++slot) {
            const float angle = static_cast<float>(i) * 0.01f + static_cast<float>(slot);
            inputs[slot].moveX = std::cos(angle);
            inputs[slot].moveY = std::sin(angle);
        }
        sim::step(state, inputs, state.playerCount());
        state.events.clear();
        for (int slot = 0; slot < state.playerCount(); ++slot) state.players[slot].hp = state.players[slot].maxHp;
    }
}

// A snapshot of the whole state, as the host would build it for one player, with no cap on
// how many of each kind beyond the snapshot's own arrays.
std::unique_ptr<Snapshot> fullSnapshot(const GameState& state, Vec2 origin) {
    auto s = std::make_unique<Snapshot>();
    s->header.tick = state.run.tick;
    s->header.phase = state.run.phase;
    s->header.kills = state.run.kills;
    s->header.origin = origin;
    s->header.playerCount = state.playerCount();
    s->header.ackSeq = 1234;
    for (int i = 0; i < state.playerCount(); ++i) s->players[i] = state.players[i];
    for (const Enemy& e : state.enemies) {
        if (s->enemyCount < balance::kNetMaxEnemies) s->enemies[s->enemyCount++] = e;
    }
    for (const Projectile& p : state.projectiles) {
        if (s->projectileCount < balance::kNetMaxProjectiles) s->projectiles[s->projectileCount++] = p;
    }
    for (const Gem& g : state.gems) {
        if (s->gemCount < balance::kNetMaxGems) s->gems[s->gemCount++] = g;
    }
    for (const Zone& z : state.zones) {
        if (s->zoneCount < balance::kNetMaxZones) s->zones[s->zoneCount++] = z;
    }
    for (const EnemyBullet& b : state.enemyBullets) {
        if (s->enemyBulletCount < balance::kNetMaxEnemyBullets) s->enemyBullets[s->enemyBulletCount++] = b;
    }
    for (const Pickup& p : state.pickups) s->pickups[s->pickupCount++] = p;
    return s;
}

constexpr float kHalfStep = 0.5f / kPositionScale + 1e-3f;  // The most a position can be off by.

}  // namespace

// ---- Codec --------------------------------------------------------------------------

TEST_CASE("net codec: run settings round trip, and bad ones are refused") {
    RunStart start;
    start.runNumber = 3;
    start.seed = 0x1234567890ABCDEFull;
    start.map = maps::MapId::Wasteland;
    start.difficulty = Difficulty::Hard;
    start.mode = RunMode::Endless;
    start.playerCount = 3;
    start.yourSlot = 2;
    start.netIds[0] = 0;
    start.netIds[1] = 2;
    start.netIds[2] = 3;
    uint8_t bytes[64];
    const int size = encodeRunStart(start, bytes, sizeof(bytes));
    REQUIRE(size > 0);
    CHECK(kindOf(bytes, size) == static_cast<uint8_t>(MessageKind::RunStart));
    RunStart back;
    REQUIRE(decodeRunStart(bytes, size, back));
    CHECK(back.runNumber == 3);
    CHECK(back.seed == start.seed);
    CHECK(back.map == maps::MapId::Wasteland);
    CHECK(back.difficulty == Difficulty::Hard);
    CHECK(back.mode == RunMode::Endless);
    CHECK(back.playerCount == 3);
    CHECK(back.yourSlot == 2);
    CHECK(back.netIds[1] == 2);
    CHECK(back.netIds[2] == 3);

    // Every shorter packet is refused, and so is a longer one.
    for (int cut = 0; cut < size; ++cut) CHECK_FALSE(decodeRunStart(bytes, cut, back));
    uint8_t longer[65];
    std::memcpy(longer, bytes, static_cast<size_t>(size));
    longer[size] = 0;
    CHECK_FALSE(decodeRunStart(longer, size + 1, back));
    // Another protocol version, a bad map, and a slot outside the team.
    uint8_t broken[64];
    std::memcpy(broken, bytes, static_cast<size_t>(size));
    broken[1] = kProtocolVersion + 1;
    CHECK_FALSE(decodeRunStart(broken, size, back));
    CHECK(kindOf(broken, size) == 0);
    std::memcpy(broken, bytes, static_cast<size_t>(size));
    broken[11] = 9;  // The map.
    CHECK_FALSE(decodeRunStart(broken, size, back));
    std::memcpy(broken, bytes, static_cast<size_t>(size));
    broken[15] = 5;  // Your slot, past the 3 players.
    CHECK_FALSE(decodeRunStart(broken, size, back));
    // A buffer too small to write into gives nothing rather than half a packet.
    CHECK(encodeRunStart(start, bytes, 8) == 0);
}

TEST_CASE("net codec: inputs keep their steps, and movement is rounded to 1/127") {
    InputMessage message;
    message.runNumber = 1;
    message.newestSeq = 500;
    message.count = balance::kNetInputRepeat;
    for (int i = 0; i < message.count; ++i) message.samples[i] = quantizeInput(0.3f * static_cast<float>(i) - 1.0f, -0.7f, i == 2);
    uint8_t bytes[64];
    const int size = encodeInput(message, bytes, sizeof(bytes));
    REQUIRE(size > 0);
    CHECK(size <= 10 + 3 * balance::kNetInputRepeat);
    InputMessage back;
    REQUIRE(decodeInput(bytes, size, back));
    CHECK(back.newestSeq == 500);
    REQUIRE(back.count == message.count);
    for (int i = 0; i < back.count; ++i) {
        const float wanted = clampf(0.3f * static_cast<float>(i) - 1.0f, -1.0f, 1.0f);
        CHECK(std::abs(unquantizeMove(back.samples[i].moveX) - wanted) <= 0.5f / 127.0f + 1e-5f);
        CHECK(std::abs(unquantizeMove(back.samples[i].moveY) + 0.7f) <= 0.5f / 127.0f + 1e-5f);
        CHECK(back.samples[i].dash == (i == 2));
    }
    // Full speed is exactly full speed, both ways.
    CHECK(unquantizeMove(quantizeInput(1.0f, -1.0f, false).moveX) == 1.0f);
    CHECK(unquantizeMove(quantizeInput(1.0f, -1.0f, false).moveY) == -1.0f);
    for (int cut = 0; cut < size; ++cut) CHECK_FALSE(decodeInput(bytes, cut, back));
    // No samples, or more than are ever sent, is not an input.
    uint8_t broken[64];
    std::memcpy(broken, bytes, static_cast<size_t>(size));
    broken[9] = 0;  // The sample count.
    CHECK_FALSE(decodeInput(broken, size, back));
    broken[9] = static_cast<uint8_t>(balance::kNetInputRepeat + 1);
    CHECK_FALSE(decodeInput(broken, size, back));
}

TEST_CASE("net codec: small reliable messages round trip") {
    uint8_t bytes[2048];
    CardPick pick;
    pick.runNumber = 4;
    pick.offerSerial = 300;
    pick.card = 2;
    int size = encodeCardPick(pick, bytes, sizeof(bytes));
    CardPick pickBack;
    REQUIRE(decodeCardPick(bytes, size, pickBack));
    CHECK(pickBack.card == 2);
    CHECK(pickBack.offerSerial == 300);
    bytes[5] = 7;  // A card that is not on screen.
    CHECK_FALSE(decodeCardPick(bytes, size, pickBack));

    CrushedMessage crushed;
    crushed.runNumber = 2;
    crushed.firstIndex = 10;
    crushed.count = 3;
    for (int i = 0; i < 3; ++i) crushed.cars[i] = maps::CarId{-100 * i, 7 + i};
    size = encodeCrushed(crushed, bytes, sizeof(bytes));
    CrushedMessage crushedBack;
    REQUIRE(decodeCrushed(bytes, size, crushedBack));
    CHECK(crushedBack.firstIndex == 10);
    CHECK(crushedBack.cars[2].texelX == -200);
    CHECK(crushedBack.cars[2].texelY == 9);
    CHECK_FALSE(decodeCrushed(bytes, size - 1, crushedBack));

    PauseMessage pause{5, true, true};
    size = encodePause(pause, bytes, sizeof(bytes));
    PauseMessage pauseBack;
    REQUIRE(decodePause(bytes, size, pauseBack));
    CHECK(pauseBack.paused);
    CHECK(pauseBack.away);

    RunEnd end;
    end.runNumber = 1;
    end.phase = RunPhase::Won;
    end.tick = 36000;
    end.kills = 4321;
    end.playerCount = 2;
    end.levels[1] = 17;
    end.damageByWeapon[1][3] = 999.5f;
    size = encodeRunEnd(end, bytes, sizeof(bytes));
    RunEnd endBack;
    REQUIRE(decodeRunEnd(bytes, size, endBack));
    CHECK(endBack.phase == RunPhase::Won);
    CHECK(endBack.kills == 4321);
    CHECK(endBack.levels[1] == 17);
    CHECK(endBack.damageByWeapon[1][3] == 999.5f);
    CHECK_FALSE(decodeRunEnd(bytes, size - 4, endBack));
}

TEST_CASE("net codec: a snapshot of a real run round trips within the rounding") {
    auto state = coopState(2);
    play(*state, 60 * 40);
    REQUIRE(state->enemies.count > 20);
    REQUIRE(state->projectiles.count > 0);
    const Vec2 origin = state->players[1].pos;
    auto snap = fullSnapshot(*state, origin);
    std::vector<uint8_t> bytes(kMaxMessageBytes);
    const int size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    REQUIRE(size > 0);
    CHECK(size <= balance::kNetSnapshotBytes);
    auto back = std::make_unique<Snapshot>();
    REQUIRE(decodeSnapshot(bytes.data(), size, *back));
    CHECK(back->header.tick == state->run.tick);
    CHECK(back->header.ackSeq == 1234u);
    CHECK(back->header.playerCount == 2);

    // Players are exact where it matters for the HUD and the prediction.
    for (int slot = 0; slot < 2; ++slot) {
        const Player& a = state->players[slot];
        const Player& b = back->players[slot];
        CHECK(b.pos.x == a.pos.x);
        CHECK(b.pos.y == a.pos.y);
        CHECK(b.hp == a.hp);
        CHECK(b.level == a.level);
        CHECK(b.xp == a.xp);
        CHECK(b.weaponCount == a.weaponCount);
        CHECK(b.stats.moveSpeed == a.stats.moveSpeed);
        CHECK(b.rerollsLeft == a.rerollsLeft);
        for (int w = 0; w < a.weaponCount; ++w) {
            CHECK(b.weapons[w].id == a.weapons[w].id);
            CHECK(b.weapons[w].magazine == a.weapons[w].magazine);
            CHECK(std::abs(b.weapons[w].reloadLeft - a.weapons[w].reloadLeft) <= 0.0006f);
        }
        // A direction is one of 256, so it is off by at most half a step.
        CHECK(dot(b.facing, normalizedOr(a.facing, Vec2{1, 0})) >= std::cos(kTau / 512.0f) - 1e-4f);
    }
    // Everything else is within half an eighth of a unit of where it really is.
    REQUIRE(back->enemyCount == snap->enemyCount);
    for (int i = 0; i < back->enemyCount; ++i) {
        const Enemy& a = snap->enemies[i];
        const Enemy& b = back->enemies[i];
        CHECK(shortId(a.id) == b.id);
        CHECK(a.type == b.type);
        CHECK(a.elite == b.elite);
        CHECK(std::abs(a.pos.x - b.pos.x) <= kHalfStep);
        CHECK(std::abs(a.pos.y - b.pos.y) <= kHalfStep);
    }
    REQUIRE(back->projectileCount == snap->projectileCount);
    for (int i = 0; i < back->projectileCount; ++i) {
        CHECK(std::abs(snap->projectiles[i].pos.x - back->projectiles[i].pos.x) <= kHalfStep);
        CHECK(std::abs(snap->projectiles[i].vel.y - back->projectiles[i].vel.y) <= 0.5f + 1e-3f);
        CHECK(snap->projectiles[i].kind == back->projectiles[i].kind);
    }
    REQUIRE(back->gemCount == snap->gemCount);
    for (int i = 0; i < back->gemCount; ++i) CHECK(std::abs(snap->gems[i].pos.y - back->gems[i].pos.y) <= kHalfStep);
}

TEST_CASE("net codec: a crowded snapshot stops at the size cap, keeps the most important parts, and still reads") {
    auto state = coopState(4);
    sim::setStressTargets(*state, 2000, 3000);
    play(*state, 30);
    REQUIRE(state->enemies.count >= 1900);
    REQUIRE(state->projectiles.count >= 2500);
    auto snap = fullSnapshot(*state, state->players[2].pos);
    REQUIRE(snap->enemyCount == balance::kNetMaxEnemies);
    std::vector<uint8_t> bytes(kMaxMessageBytes);
    const int size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    REQUIRE(size > 0);
    CHECK(size <= balance::kNetSnapshotBytes);
    CHECK(size > balance::kNetSnapshotBytes - 64);  // It was full: the lists were cut.
    auto back = std::make_unique<Snapshot>();
    REQUIRE(decodeSnapshot(bytes.data(), size, *back));
    CHECK(back->header.playerCount == 4);
    // The players and every enemy bullet came first. The gems, last in line, had no room.
    CHECK(back->enemyBulletCount == snap->enemyBulletCount);
    CHECK(back->enemyCount > 0);
    CHECK(back->gemCount == 0);
    CHECK(back->enemyCount + back->projectileCount < snap->enemyCount + snap->projectileCount);
}

TEST_CASE("net codec: short, cut and scrambled snapshots never read past their end") {
    auto state = coopState(3);
    play(*state, 60 * 25);
    auto snap = fullSnapshot(*state, state->players[1].pos);
    std::vector<uint8_t> bytes(kMaxMessageBytes);
    const int size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    REQUIRE(size > 100);
    auto back = std::make_unique<Snapshot>();
    // Every cut inside the header or the players is refused. Past them, a cut can land exactly
    // between two lists, which reads as a smaller snapshot, but never as a broken one.
    int accepted = 0;
    for (int cut = 0; cut < size; ++cut) {
        // The copy is exactly as long as the cut, so reading past it would be caught by the
        // address checker in a debug build, and by the length checks here.
        std::vector<uint8_t> shorter(bytes.begin(), bytes.begin() + cut);
        if (decodeSnapshot(shorter.data(), cut, *back)) {
            accepted++;
            CHECK(back->header.playerCount == 3);
        }
    }
    CHECK(accepted < 8);
    // Random damage: decoding either fails or gives counts inside the arrays.
    Rng rng;
    rng.seed(4);
    for (int trial = 0; trial < 3000; ++trial) {
        std::vector<uint8_t> broken(bytes.begin(), bytes.begin() + size);
        const int flips = 1 + static_cast<int>(rng.nextU32() % 6);
        for (int f = 0; f < flips; ++f) broken[rng.nextU32() % static_cast<uint32_t>(size)] = static_cast<uint8_t>(rng.nextU32());
        broken[0] = static_cast<uint8_t>(MessageKind::Snapshot);
        broken[1] = kProtocolVersion;
        if (decodeSnapshot(broken.data(), size, *back)) {
            CHECK(back->enemyCount <= balance::kNetMaxEnemies);
            CHECK(back->projectileCount <= balance::kNetMaxProjectiles);
            CHECK(back->header.playerCount <= kMaxPlayers);
            for (int i = 0; i < back->header.playerCount; ++i) CHECK(back->players[i].weaponCount <= kMaxHeldWeapons);
        }
    }
    // A snapshot is not an input, and nothing is read from an empty packet.
    InputMessage input;
    CHECK_FALSE(decodeInput(bytes.data(), size, input));
    CHECK_FALSE(decodeSnapshot(bytes.data(), 0, *back));
    CHECK_FALSE(decodeSnapshot(nullptr, 0, *back));
}

// ---- Host and client together -----------------------------------------------------

namespace {

// Two machines in one program. Each message waits in the pipe until the other side reads it,
// after a delay counted in frames. Unreliable messages can be lost on purpose.
struct Pipe {
    struct Message {
        int from;
        int to;
        int readyFrame;
        bool reliable;
        std::vector<uint8_t> bytes;
    };
    std::deque<Message> messages;
    int frame = 0;
    int delayFrames = 2;
    float lossShare = 0.0f;
    Rng rng;
    int sentUnreliable = 0;
    int maxSnapshotBytes = 0;
    // The next dropCount messages of this kind are lost, even reliable ones, as when the
    // browser's queue overflows.
    MessageKind dropKind = MessageKind::RunStart;
    int dropCount = 0;
    int dropped = 0;
};

class PipeEnd final : public Link {
public:
    PipeEnd(Pipe& pipe, int me) : pipe_(pipe), me_(me) {}
    bool send(int to, const uint8_t* data, int size, bool reliable) override {
        if (pipe_.dropCount > 0 && kindOf(data, size) == static_cast<uint8_t>(pipe_.dropKind)) {
            pipe_.dropCount--;
            pipe_.dropped++;
            return true;
        }
        if (!reliable) {
            pipe_.sentUnreliable++;
            if (pipe_.rng.next01() < pipe_.lossShare) return true;
            if (kindOf(data, size) == static_cast<uint8_t>(MessageKind::Snapshot) && size > pipe_.maxSnapshotBytes) pipe_.maxSnapshotBytes = size;
        }
        pipe_.messages.push_back(Pipe::Message{me_, to, pipe_.frame + pipe_.delayFrames, reliable, std::vector<uint8_t>(data, data + size)});
        return true;
    }
    int receive(uint8_t* buffer, int capacity, int* from) override {
        for (auto it = pipe_.messages.begin(); it != pipe_.messages.end(); ++it) {
            if (it->to != me_ || it->readyFrame > pipe_.frame) continue;
            const int size = static_cast<int>(it->bytes.size());
            if (size > capacity) {
                pipe_.messages.erase(it);
                return 0;
            }
            std::memcpy(buffer, it->bytes.data(), static_cast<size_t>(size));
            *from = it->from;
            pipe_.messages.erase(it);
            return size;
        }
        return 0;
    }

private:
    Pipe& pipe_;
    int me_;
};

// A host (lobby slot 0) and one client (lobby slot 2, as if slot 1 had left the lobby).
struct Rig {
    Pipe pipe;
    PipeEnd hostEnd{pipe, 0};
    PipeEnd clientEnd{pipe, 2};
    Session* host = createSession();
    Session* client = createSession();
    std::unique_ptr<GameState> hostState = std::make_unique<GameState>();
    std::unique_ptr<GameState> clientState = std::make_unique<GameState>();
    float hostClock = 0.0f;

    Rig() {
        useSession(host);
        setLink(&hostEnd);
        useSession(client);
        setLink(&clientEnd);
        clientListen();
        useSession(host);
        RunStart start;
        start.seed = 31337;
        start.playerCount = 2;
        start.netIds[0] = 0;
        start.netIds[1] = 2;
        hostStart(*hostState, start);
        useSession(nullptr);
    }
    ~Rig() {
        destroySession(host);
        destroySession(client);
        useSession(nullptr);
    }

    // One frame of 1/60 s on both machines. The host's own player stands still.
    void frame(float clientMoveX, float clientMoveY, bool clientDash = false) {
        pipe.frame++;
        useSession(host);
        hostReceive();
        hostClock += kDt;
        while (hostClock >= kDt - 1e-6f) {
            hostClock -= kDt;
            InputFrame inputs[kMaxPlayers];
            hostFillInputs(*hostState, inputs, hostState->playerCount());
            hostBeforeStep(*hostState);
            sim::step(*hostState, inputs, hostState->playerCount());
        }
        hostAfterFrame(*hostState, kDt, false, false);
        hostState->events.clear();
        useSession(client);
        clientNewRun(*clientState);
        clientFrame(*clientState, kDt, clientMoveX, clientMoveY, clientDash);
        clientState->events.clear();
        useSession(nullptr);
    }

    // The client's own soldier as the client predicts it, without the smoothing offset.
    Vec2 clientSeesSelf() const { return clientState->players[1].pos; }
};

}  // namespace

TEST_CASE("net session: the client gets the run's settings and the same starting world") {
    Rig rig;
    for (int i = 0; i < 10; ++i) rig.frame(0.0f, 0.0f);
    CHECK(rig.clientState->run.seed == 31337u);
    CHECK(rig.clientState->playerCount() == 2);
    CHECK(rig.clientState->localSlot == 1);
    CHECK(rig.hostState->localSlot == 0);
    CHECK(distance(rig.clientState->players[1].pos, rig.hostState->players[1].pos) < 0.01f);
}

TEST_CASE("net session: the client's prediction and the host agree on where the client walked") {
    Rig rig;
    for (int i = 0; i < 30; ++i) rig.frame(0.0f, 0.0f);
    // Walk right for two seconds, then down and to the left with a dash, then stop.
    for (int i = 0; i < 120; ++i) rig.frame(1.0f, 0.0f);
    for (int i = 0; i < 60; ++i) rig.frame(-0.6f, 0.8f, i == 10);
    for (int i = 0; i < 60; ++i) rig.frame(0.0f, 0.0f);
    const Vec2 host = rig.hostState->players[1].pos;
    const Vec2 client = rig.clientSeesSelf();
    CHECK(length(host - Vec2{}) > 150.0f);  // It really moved.
    CHECK(distance(host, client) < 1.0f);
    // The host saw the dash.
    CHECK(rig.hostState->players[1].dashCooldown > 0.0f);
    // The client sees the host's soldier where the host has it.
    CHECK(distance(rig.clientState->players[0].pos, rig.hostState->players[0].pos) < 0.2f);
}

TEST_CASE("net session: the client sees the zombies near it, close to where the host has them") {
    Rig rig;
    for (int i = 0; i < 60 * 45; ++i) rig.frame(std::cos(static_cast<float>(i) * 0.01f), std::sin(static_cast<float>(i) * 0.013f));
    useSession(rig.host);
    const int near = hostNearEnemies(1);
    useSession(nullptr);
    REQUIRE(near > 10);
    // The client draws a few steps in the past, so a zombie or two may have come or gone since.
    CHECK(std::abs(rig.clientState->enemies.count - std::min(near, balance::kNetMaxEnemies)) <= std::max(3, near / 10));
    // Each zombie the client draws stands near a real one: the drawing is a few steps behind.
    int matched = 0;
    for (const Enemy& seen : rig.clientState->enemies) {
        for (const Enemy& real : rig.hostState->enemies) {
            if (shortId(real.id) == seen.id && distance(real.pos, seen.pos) < 30.0f) {
                matched++;
                break;
            }
        }
    }
    CHECK(matched >= rig.clientState->enemies.count * 9 / 10);
    CHECK(rig.pipe.maxSnapshotBytes <= balance::kNetSnapshotBytes);
    CHECK(rig.clientState->run.kills == rig.hostState->run.kills);
}

TEST_CASE("net session: with a third of the packets lost and a slow link, the prediction still ends where the host is") {
    Rig rig;
    rig.pipe.lossShare = 0.33f;
    rig.pipe.delayFrames = 9;  // About 150 ms each way.
    rig.pipe.rng.seed(11);
    for (int i = 0; i < 40; ++i) rig.frame(0.0f, 0.0f);
    for (int i = 0; i < 240; ++i) rig.frame(std::cos(static_cast<float>(i) * 0.03f), std::sin(static_cast<float>(i) * 0.03f));
    for (int i = 0; i < 90; ++i) rig.frame(0.0f, 0.0f);
    // Lost inputs made the two disagree on the way, and the client was put right.
    useSession(rig.client);
    CHECK(stats().corrections > 0);
    useSession(nullptr);
    CHECK(distance(rig.hostState->players[1].pos, rig.clientSeesSelf()) < 1.0f);
}

TEST_CASE("net session: a card picked on the client is taken by the host, once") {
    Rig rig;
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    upgrades::addXp(*rig.hostState, 1, rig.hostState->players[1].xpToNext - rig.hostState->players[1].xp);  // The next step levels up.
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    REQUIRE(rig.hostState->players[1].cardsOpen);
    REQUIRE(rig.clientState->players[1].cardsOpen);
    const int weaponsBefore = rig.hostState->players[1].weaponCount;
    const int passivesBefore = rig.hostState->players[1].passiveCount;
    useSession(rig.client);
    clientPickCard(*rig.clientState, 0, false);
    clientPickCard(*rig.clientState, 1, false);  // A second press on the same cards is ignored.
    useSession(nullptr);
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    CHECK_FALSE(rig.hostState->players[1].cardsOpen);
    CHECK(rig.hostState->players[1].weaponCount + rig.hostState->players[1].passiveCount >= weaponsBefore + passivesBefore);
    CHECK_FALSE(rig.clientState->players[1].cardsOpen);
}

TEST_CASE("net session: a player who left stays down, and the host keeps a late player's last input only briefly") {
    Rig rig;
    for (int i = 0; i < 20; ++i) rig.frame(1.0f, 0.0f);
    // The client goes quiet: the host keeps it walking for a moment, then it stops.
    const Vec2 before = rig.hostState->players[1].pos;
    useSession(rig.host);
    for (int i = 0; i < 60; ++i) {
        rig.pipe.frame++;
        rig.pipe.messages.clear();
        hostReceive();
        InputFrame inputs[kMaxPlayers];
        hostFillInputs(*rig.hostState, inputs, 2);
        sim::step(*rig.hostState, inputs, 2);
        rig.hostState->events.clear();
    }
    const float walked = distance(before, rig.hostState->players[1].pos);
    CHECK(walked > 1.0f);
    CHECK(walked < balance::kPlayerSpeed * kDt * static_cast<float>(balance::kNetHoldInputSteps + 12));

    hostSetConnected(1, false);
    for (int i = 0; i < 5; ++i) {
        InputFrame inputs[kMaxPlayers];
        hostFillInputs(*rig.hostState, inputs, 2);
        hostBeforeStep(*rig.hostState);
        sim::step(*rig.hostState, inputs, 2);
        rig.hostState->events.clear();
    }
    CHECK(rig.hostState->players[1].downed);
    // The host stands right on top of them, and still they stay down.
    rig.hostState->players[0].pos = rig.hostState->players[1].pos;
    for (int i = 0; i < 60 * 5; ++i) {
        InputFrame inputs[kMaxPlayers];
        hostFillInputs(*rig.hostState, inputs, 2);
        hostBeforeStep(*rig.hostState);
        sim::step(*rig.hostState, inputs, 2);
        rig.hostState->events.clear();
        rig.hostState->players[0].hp = rig.hostState->players[0].maxHp;
    }
    CHECK(rig.hostState->players[1].downed);
    useSession(nullptr);
}

TEST_CASE("net session: the host's pause and the end of the run reach the client") {
    Rig rig;
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    useSession(rig.host);
    hostSendPause(false, true);
    useSession(nullptr);
    rig.pipe.frame += 5;
    useSession(rig.client);
    clientFrame(*rig.clientState, kDt, 0.0f, 0.0f, false);
    CHECK(clientHostAway());
    useSession(nullptr);
    rig.hostState->run.phase = RunPhase::Dead;
    for (int i = 0; i < 10; ++i) rig.frame(0.0f, 0.0f);
    useSession(rig.client);
    CHECK(clientRunEnded());
    CHECK(clientRunEnd().phase == RunPhase::Dead);
    useSession(nullptr);
}

// ---- Fixes from review ------------------------------------------------------------

TEST_CASE("net session: a dash the host has not used yet keeps its cooldown on the client") {
    Rig rig;
    rig.pipe.delayFrames = 9;  // Snapshots built before the host got the dash arrive after it.
    for (int i = 0; i < 40; ++i) rig.frame(0.0f, 0.0f);
    rig.frame(1.0f, 0.0f, true);
    float lowest = 10.0f;
    for (int i = 0; i < 40; ++i) {
        rig.frame(1.0f, 0.0f);
        lowest = std::min(lowest, rig.clientState->players[1].dashCooldown);
    }
    // The button stays locked for the whole cooldown, as it is on the host.
    CHECK(lowest > balance::kDashCooldownSeconds - 41.0f * kDt - 0.2f);
    CHECK(rig.hostState->players[1].dashCooldown > 0.0f);
}

TEST_CASE("net session: a card pick names its cards, so a pick for older cards is dropped, and a lost pick is sent again") {
    Rig rig;
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    upgrades::addXp(*rig.hostState, 1, rig.hostState->players[1].xpToNext - rig.hostState->players[1].xp);
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    REQUIRE(rig.hostState->players[1].cardsOpen);
    const uint16_t serial = rig.hostState->players[1].offerSerial;
    CHECK(rig.clientState->players[1].offerSerial == serial);

    // A pick for cards the player no longer sees is ignored.
    CardPick stale;
    stale.runNumber = 1;
    stale.offerSerial = static_cast<uint16_t>(serial - 1);
    stale.card = 0;
    uint8_t bytes[16];
    const int size = encodeCardPick(stale, bytes, sizeof(bytes));
    rig.pipe.messages.push_back(Pipe::Message{2, 0, rig.pipe.frame, true, std::vector<uint8_t>(bytes, bytes + size)});
    for (int i = 0; i < 5; ++i) rig.frame(0.0f, 0.0f);
    CHECK(rig.hostState->players[1].cardsOpen);
    CHECK(rig.hostState->players[1].level == 2);

    // The real pick is lost on the way, and sent again.
    rig.pipe.dropKind = MessageKind::CardPick;
    rig.pipe.dropCount = 1;
    useSession(rig.client);
    clientPickCard(*rig.clientState, 0, false);
    useSession(nullptr);
    for (int i = 0; i < 90; ++i) rig.frame(0.0f, 0.0f);
    CHECK(rig.pipe.dropped == 1);
    CHECK_FALSE(rig.hostState->players[1].cardsOpen);
    CHECK_FALSE(rig.clientState->players[1].cardsOpen);
}

TEST_CASE("net session: the end of the run and flat cars arrive even when a copy is lost") {
    Rig rig;
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    // Two flat cars, and the message about them is lost.
    rig.pipe.dropKind = MessageKind::Crushed;
    rig.pipe.dropCount = 1;
    rig.hostState->run.crushed[0] = maps::CarId{123, 456};
    rig.hostState->run.crushed[1] = maps::CarId{789, 12};
    rig.hostState->run.crushedCount = 2;
    for (int i = 0; i < 120; ++i) rig.frame(0.0f, 0.0f);
    CHECK(rig.pipe.dropped == 1);
    REQUIRE(rig.clientState->run.crushedCount == 2);
    CHECK(rig.clientState->run.crushed[1].texelX == 789);

    rig.pipe.dropKind = MessageKind::RunEnd;
    rig.pipe.dropCount = 1;
    rig.hostState->run.phase = RunPhase::Won;
    for (int i = 0; i < 90; ++i) rig.frame(0.0f, 0.0f);
    CHECK(rig.pipe.dropped == 2);
    useSession(rig.client);
    CHECK(clientRunEnded());
    CHECK(clientRunEnd().phase == RunPhase::Won);
    useSession(nullptr);
}

TEST_CASE("net session: another run is made from the room as it is now") {
    Pipe pipe;
    PipeEnd end{pipe, 0};
    Session* session = createSession();
    useSession(session);
    setLink(&end);
    auto state = std::make_unique<GameState>();
    RunStart start;
    start.runNumber = 1;
    start.seed = 5;
    start.playerCount = 3;
    start.netIds[0] = 0;
    start.netIds[1] = 1;
    start.netIds[2] = 3;
    hostStart(*state, start);
    hostSetConnected(1, false);  // Lobby slot 1 leaves.
    RunStart again = start;
    again.seed = 6;
    again.playerCount = 2;
    again.netIds[1] = 3;
    again.netIds[2] = 255;
    hostRestart(*state, again);
    CHECK(state->playerCount() == 2);
    CHECK(settings().runNumber == 2);
    CHECK(settings().seed == 6u);
    CHECK(netIdOf(1) == 3);
    CHECK(hostConnected(1));
    CHECK(netIdOf(2) == -1);
    destroySession(session);
}

TEST_CASE("net session: a dash in an input step the host drops still happens") {
    Pipe pipe;
    PipeEnd end{pipe, 0};
    Session* session = createSession();
    useSession(session);
    setLink(&end);
    auto state = std::make_unique<GameState>();
    RunStart start;
    start.runNumber = 1;
    start.seed = 8;
    start.playerCount = 2;
    start.netIds[0] = 0;
    start.netIds[1] = 2;
    hostStart(*state, start);
    // Twelve steps arrive at once, after a stall. Only the newest eight are kept, and the
    // dash was in step 2.
    for (uint32_t newest : {6u, 12u}) {
        InputMessage message;
        message.runNumber = 1;
        message.newestSeq = newest;
        message.count = 6;
        for (int i = 0; i < 6; ++i) message.samples[i] = quantizeInput(0.0f, 0.0f, newest - static_cast<uint32_t>(i) == 2u);
        uint8_t bytes[64];
        const int size = encodeInput(message, bytes, sizeof(bytes));
        pipe.messages.push_back(Pipe::Message{2, 0, 0, false, std::vector<uint8_t>(bytes, bytes + size)});
    }
    hostReceive();
    for (int i = 0; i < 3; ++i) {
        InputFrame inputs[kMaxPlayers];
        hostFillInputs(*state, inputs, 2);
        sim::step(*state, inputs, 2);
        state->events.clear();
    }
    CHECK(state->players[1].dashCooldown > 0.0f);
    destroySession(session);
}

TEST_CASE("net codec: zombie targets travel, and out-of-range levels, details and steps are refused") {
    auto state = coopState(3);
    play(*state, 60 * 20);
    REQUIRE(state->enemies.count > 0);
    auto snap = fullSnapshot(*state, state->players[0].pos);
    snap->enemies[0].target = 2;
    std::vector<uint8_t> bytes(kMaxMessageBytes);
    int size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    auto back = std::make_unique<Snapshot>();
    REQUIRE(decodeSnapshot(bytes.data(), size, *back));
    CHECK(back->enemies[0].target == 2);
    CHECK(back->players[1].offerSerial == state->players[1].offerSerial);

    // A weapon at level 0 would read before the start of the level table.
    snap->players[1].weapons[0].level = 0;
    size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    CHECK_FALSE(decodeSnapshot(bytes.data(), size, *back));
    snap->players[1].weapons[0].level = 6;
    size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    CHECK_FALSE(decodeSnapshot(bytes.data(), size, *back));
    snap->players[1].weapons[0].level = 1;

    // An event naming a weapon that does not exist.
    snap->eventCount = 1;
    snap->events[0] = SimEvent{};
    snap->events[0].type = EventType::WeaponFired;
    snap->events[0].detail = static_cast<uint8_t>(WeaponId::Count);
    size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    CHECK_FALSE(decodeSnapshot(bytes.data(), size, *back));
    snap->events[0].detail = static_cast<uint8_t>(WeaponId::Rifle);
    size = encodeSnapshot(*snap, bytes.data(), balance::kNetSnapshotBytes);
    CHECK(decodeSnapshot(bytes.data(), size, *back));

    // Input steps start at 1, so six samples can't end at step 3.
    InputMessage input;
    input.runNumber = 1;
    input.newestSeq = 3;
    input.count = 6;
    uint8_t small[64];
    size = encodeInput(input, small, sizeof(small));
    InputMessage inputBack;
    CHECK_FALSE(decodeInput(small, size, inputBack));
    input.newestSeq = 6;
    size = encodeInput(input, small, sizeof(small));
    CHECK(decodeInput(small, size, inputBack));
}

// ---- Rejoin ------------------------------------------------------------------------

namespace {

// One host frame with nobody else: the client's machine is gone, so it runs nothing.
void hostAlone(Rig& rig) {
    rig.pipe.frame++;
    rig.pipe.messages.clear();
    useSession(rig.host);
    hostReceive();
    InputFrame inputs[kMaxPlayers];
    hostFillInputs(*rig.hostState, inputs, rig.hostState->playerCount());
    hostBeforeStep(*rig.hostState);
    sim::step(*rig.hostState, inputs, rig.hostState->playerCount());
    hostAfterFrame(*rig.hostState, kDt, false, false);
    rig.hostState->events.clear();
    rig.hostState->players[0].hp = rig.hostState->players[0].maxHp;
    useSession(nullptr);
}

// The client's page is reloaded: its machine forgets everything, and it listens again from
// nothing, as a page opened fresh does after its Rejoin was accepted by the server.
void reloadClient(Rig& rig) {
    destroySession(rig.client);
    rig.client = createSession();
    useSession(rig.client);
    setLink(&rig.clientEnd);
    clientListen();
    useSession(nullptr);
    rig.clientState = std::make_unique<GameState>();
}

// What the game does when the lobby shows the player back.
void hostSeesBack(Rig& rig) {
    useSession(rig.host);
    hostPlayerBack(1);
    useSession(nullptr);
}

// Runs frames with the host standing on top of player 1 until they are up again.
void reviveClient(Rig& rig) {
    for (int i = 0; i < 60 * 5 && rig.hostState->players[1].downed; ++i) {
        rig.hostState->players[0].pos = rig.hostState->players[1].pos;
        rig.hostState->players[0].hp = rig.hostState->players[0].maxHp;
        rig.frame(0.0f, 0.0f);
    }
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
}

int buildSize(const Player& p) { return p.weaponCount + p.passiveCount; }

}  // namespace

TEST_CASE("net session: a guest who reloads mid-run gets the run again and the same soldier, level and build") {
    Rig rig;
    for (int i = 0; i < 40; ++i) rig.frame(1.0f, 0.0f);
    // A level and a card, so the player has a build worth keeping.
    upgrades::addXp(*rig.hostState, 1, rig.hostState->players[1].xpToNext - rig.hostState->players[1].xp);
    for (int i = 0; i < 20; ++i) rig.frame(0.0f, 0.0f);
    REQUIRE(rig.clientState->players[1].cardsOpen);
    useSession(rig.client);
    clientPickCard(*rig.clientState, 0, false);
    useSession(nullptr);
    for (int i = 0; i < 30; ++i) rig.frame(0.0f, 0.0f);
    REQUIRE_FALSE(rig.hostState->players[1].cardsOpen);
    const int level = rig.hostState->players[1].level;
    const int xp = rig.hostState->players[1].xp;
    const int build = buildSize(rig.hostState->players[1]);
    REQUIRE(level >= 2);

    // The page goes away. The host holds the player down while they are gone.
    useSession(rig.host);
    hostSetConnected(1, false);
    useSession(nullptr);
    for (int i = 0; i < 90; ++i) hostAlone(rig);
    CHECK(rig.hostState->players[1].downed);
    // A car flattened meanwhile: the new page must still learn of it.
    rig.hostState->run.crushed[0] = maps::CarId{321, 654};
    rig.hostState->run.crushedCount = 1;
    const uint32_t tickAway = rig.hostState->run.tick;

    // The page comes back.
    reloadClient(rig);
    hostSeesBack(rig);
    for (int i = 0; i < 60; ++i) rig.frame(0.0f, 0.0f);
    const Player& seen = rig.clientState->players[1];
    CHECK(rig.clientState->localSlot == 1);
    CHECK(rig.clientState->run.seed == 31337u);
    CHECK(rig.clientState->run.tick > tickAway);  // The run went on: it was not started again.
    CHECK(rig.hostState->players[1].level == level);
    CHECK(rig.hostState->players[1].xp == xp);
    CHECK(buildSize(rig.hostState->players[1]) == build);
    CHECK(seen.level == level);
    CHECK(seen.xp == xp);
    CHECK(buildSize(seen) == build);
    CHECK(seen.downed);  // Still down, as the host has them.
    CHECK(distance(seen.pos, rig.hostState->players[1].pos) < 1.0f);
    CHECK(rig.clientState->run.crushedCount == 1);
    useSession(rig.host);
    CHECK(hostConnected(1));
    useSession(nullptr);

    // A teammate can get them up as usual, and then they walk, and both agree where they are.
    reviveClient(rig);
    REQUIRE_FALSE(rig.hostState->players[1].downed);
    CHECK_FALSE(rig.clientState->players[1].downed);
    const Vec2 start = rig.hostState->players[1].pos;
    for (int i = 0; i < 120; ++i) {
        rig.hostState->players[1].hp = rig.hostState->players[1].maxHp;
        rig.frame(0.0f, 1.0f);
    }
    for (int i = 0; i < 60; ++i) rig.frame(0.0f, 0.0f);
    CHECK(distance(start, rig.hostState->players[1].pos) > 100.0f);
    CHECK(distance(rig.hostState->players[1].pos, rig.clientSeesSelf()) < 1.0f);
}

TEST_CASE("net session: a guest whose connection drops and comes back keeps its run going, and the repeated settings change nothing") {
    Rig rig;
    for (int i = 0; i < 40; ++i) rig.frame(1.0f, 0.0f);
    // Both machines run, but nothing gets through, and the host learns the player is away.
    useSession(rig.host);
    hostSetConnected(1, false);
    useSession(nullptr);
    for (int i = 0; i < 90; ++i) {
        rig.pipe.messages.clear();
        rig.frame(1.0f, 0.0f);
    }
    useSession(rig.client);
    CHECK(clientHostAway());  // Nothing from the host for a while.
    useSession(nullptr);
    const uint32_t clientTick = rig.clientState->run.tick;
    REQUIRE(clientTick > 30u);

    hostSeesBack(rig);
    for (int i = 0; i < 60; ++i) rig.frame(0.0f, 0.0f);
    useSession(rig.client);
    CHECK_FALSE(clientHostAway());
    CHECK(settings().seed == 31337u);  // Still the first run's settings.
    useSession(nullptr);
    CHECK(rig.clientState->run.tick > clientTick);  // The same run, not a fresh one.
    CHECK(rig.clientState->players[1].downed);
    CHECK(distance(rig.clientSeesSelf(), rig.hostState->players[1].pos) < 1.0f);

    reviveClient(rig);
    REQUIRE_FALSE(rig.hostState->players[1].downed);
    for (int i = 0; i < 90; ++i) {
        rig.hostState->players[1].hp = rig.hostState->players[1].maxHp;
        rig.frame(-1.0f, 0.0f);
    }
    for (int i = 0; i < 60; ++i) rig.frame(0.0f, 0.0f);
    CHECK(distance(rig.hostState->players[1].pos, rig.clientSeesSelf()) < 1.0f);
}

TEST_CASE("net session: a player who comes back after the host began a run without them is told so, and gets no run") {
    for (const bool reload : {true, false}) {
        CAPTURE(reload);
        Rig rig;
        for (int i = 0; i < 30; ++i) rig.frame(0.0f, 0.0f);
        // The player (lobby slot 2) is away when the host plays again with someone else.
        useSession(rig.host);
        hostSetConnected(1, false);
        RunStart again;
        again.seed = 99;
        again.playerCount = 2;
        again.netIds[0] = 0;
        again.netIds[1] = 1;
        hostRestart(*rig.hostState, again);
        CHECK(slotOfNetId(2) == -1);
        useSession(nullptr);
        for (int i = 0; i < 10; ++i) hostAlone(rig);

        // They come back, with a fresh page or with the old run still on screen.
        if (reload) reloadClient(rig);
        useSession(rig.host);
        hostSendNotInRun(2);
        useSession(nullptr);
        for (int i = 0; i < 10; ++i) rig.frame(0.0f, 0.0f);
        useSession(rig.client);
        CHECK(clientNotInRun());
        CHECK(settings().runNumber != 2);  // The new run never reached them.
        useSession(nullptr);
        CHECK(rig.clientState->run.seed != 99u);
    }
}

TEST_CASE("net codec: the not-in-this-run message round trips, and broken ones are refused") {
    NotInRunMessage message;
    message.runNumber = 7;
    uint8_t bytes[16];
    const int size = encodeNotInRun(message, bytes, sizeof(bytes));
    REQUIRE(size > 0);
    CHECK(kindOf(bytes, size) == static_cast<uint8_t>(MessageKind::NotInRun));
    NotInRunMessage back;
    REQUIRE(decodeNotInRun(bytes, size, back));
    CHECK(back.runNumber == 7);
    for (int cut = 0; cut < size; ++cut) CHECK_FALSE(decodeNotInRun(bytes, cut, back));
    bytes[size] = 0;
    CHECK_FALSE(decodeNotInRun(bytes, size + 1, back));
    // Another kind of message is never read as this one.
    PauseMessage pause;
    const int pauseSize = encodePause(pause, bytes, sizeof(bytes));
    CHECK_FALSE(decodeNotInRun(bytes, pauseSize, back));
    CHECK(encodeNotInRun(message, bytes, 1) == 0);
}
