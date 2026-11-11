#include "components/Net/Backend.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// A pretend server, driven by timers and debug commands, so the screens and the tests can see
// every phase and every error before any real networking exists. It does what a real relay
// would from the player's point of view: it sleeps until woken, answers Host and Join after a
// short delay, and keeps a room with a few players in it. Nothing here touches the network.
namespace ls::net {

namespace {

constexpr float kDefaultWakeSeconds = 0.5f;
constexpr float kDefaultLatencySeconds = 0.3f;
constexpr char kFakeCode[] = "K7QXM";
const char* const kOtherNames[kMaxPlayers] = {"Host", "Ben", "Cleo", "Dev"};

void copyText(char* out, std::size_t size, const char* in) {
    std::snprintf(out, size, "%s", in ? in : "");
}

class FakeBackend final : public Backend {
public:
    void wake(Lobby& lobby) override {
        if (awake_) return;
        // A player already in a room has no use for a wake.
        if (lobby.phase != LobbyPhase::Offline) return;
        startWaking(lobby);  // Also restarts a wake that was already running, which is what Retry means.
    }

    void host(Lobby& lobby, const char* name) override {
        pending_ = Pending::Host;
        copyText(myName_, sizeof(myName_), name);
        beginRequest(lobby);
    }

    void join(Lobby& lobby, const char* code, const char* name) override {
        pending_ = Pending::Join;
        copyText(myName_, sizeof(myName_), name);
        copyText(pendingCode_, sizeof(pendingCode_), code);
        beginRequest(lobby);
    }

    void setReady(Lobby& lobby, bool ready) override {
        if (lobby.you >= 0 && lobby.you < lobby.count) lobby.players[lobby.you].ready = ready;
    }

    void start(Lobby& lobby) override {
        lobby.started = true;
        lobby.phase = LobbyPhase::Starting;
    }

    void leave(Lobby&) override { pending_ = Pending::None; }

    void update(Lobby& lobby, float dt) override {
        if (wakeStarted_ && !awake_ && wakeDelay_ >= 0.0f) {
            wakeTimer_ += dt;
            if (wakeTimer_ >= wakeDelay_) {
                awake_ = true;
                wakeStarted_ = false;
                if (pending_ == Pending::None) {
                    if (lobby.phase == LobbyPhase::Waking) lobby.phase = LobbyPhase::Offline;
                } else {
                    lobby.phase = LobbyPhase::Connecting;
                    pendingTimer_ = 0.0f;
                }
            }
        }
        if (lobby.phase == LobbyPhase::Waking) lobby.wakeSeconds += dt;
        if (pending_ != Pending::None && lobby.phase == LobbyPhase::Connecting) {
            pendingTimer_ += dt;
            if (pendingTimer_ >= latency_) answer(lobby);
        }
    }

    void debug(Lobby& lobby, int op, int value) override {
        switch (op) {
            case kDebugReset:
                *this = FakeBackend{};
                lobby = Lobby{};
                break;
            case kDebugWakeDelayMs: wakeDelay_ = value < 0 ? -1.0f : static_cast<float>(value) / 1000.0f; break;
            case kDebugLatencyMs: latency_ = static_cast<float>(std::max(0, value)) / 1000.0f; break;
            case kDebugNextError: nextError_ = static_cast<LobbyError>(value); break;
            case kDebugRoom: makeRoom(lobby, value, 0); break;
            case kDebugGuestRoom: makeRoom(lobby, value, 1); break;
            case kDebugReadyMask:
                for (int i = 0; i < lobby.count; ++i) lobby.players[i].ready = ((value >> i) & 1) != 0;
                break;
            case kDebugHostLeft: dropToError(lobby, LobbyError::HostLeft); break;
            case kDebugLoseServer: dropToError(lobby, LobbyError::ServerUnreachable); break;
            default: break;
        }
    }

private:
    enum class Pending { None, Host, Join };

    void startWaking(Lobby& lobby) {
        wakeStarted_ = true;
        wakeTimer_ = 0.0f;
        lobby.phase = LobbyPhase::Waking;
        lobby.wakeSeconds = 0.0f;
    }

    // Host and Join both start here. A sleeping server must be woken before it can answer.
    void beginRequest(Lobby& lobby) {
        if (awake_) {
            lobby.phase = LobbyPhase::Connecting;
            pendingTimer_ = 0.0f;
        } else if (wakeStarted_) {
            lobby.phase = LobbyPhase::Waking;  // The wake in progress will carry this request on.
            lobby.wakeSeconds = 0.0f;
        } else {
            startWaking(lobby);
        }
    }

    void answer(Lobby& lobby) {
        const Pending what = pending_;
        pending_ = Pending::None;
        if (nextError_ != LobbyError::None) {
            lobby = Lobby{};
            lobby.phase = LobbyPhase::Error;
            lobby.error = nextError_;
            nextError_ = LobbyError::None;
            return;
        }
        lobby = Lobby{};
        lobby.phase = LobbyPhase::InLobby;
        lobby.host = 0;
        if (what == Pending::Host) {
            copyText(lobby.code, sizeof(lobby.code), kFakeCode);
            lobby.you = 0;
            lobby.count = 1;
            lobby.players[0].id = 0;
            copyText(lobby.players[0].name, sizeof(lobby.players[0].name), myName_);
        } else {
            copyText(lobby.code, sizeof(lobby.code), pendingCode_);
            lobby.you = 1;
            lobby.count = 2;
            lobby.players[0].id = 0;
            copyText(lobby.players[0].name, sizeof(lobby.players[0].name), kOtherNames[0]);
            lobby.players[1].id = 1;
            copyText(lobby.players[1].name, sizeof(lobby.players[1].name), myName_);
        }
    }

    // A room with this many players, for tests. You are slot youSlot.
    void makeRoom(Lobby& lobby, int players, int youSlot) {
        awake_ = true;
        wakeStarted_ = false;
        pending_ = Pending::None;
        players = std::min(kMaxPlayers, std::max(youSlot + 1, players));
        lobby = Lobby{};
        lobby.phase = LobbyPhase::InLobby;
        copyText(lobby.code, sizeof(lobby.code), kFakeCode);
        lobby.host = 0;
        lobby.you = youSlot;
        lobby.count = players;
        for (int i = 0; i < players; ++i) {
            lobby.players[i].id = i;
            copyText(lobby.players[i].name, sizeof(lobby.players[i].name), i == youSlot && myName_[0] ? myName_ : kOtherNames[i]);
        }
    }

    void dropToError(Lobby& lobby, LobbyError error) {
        pending_ = Pending::None;
        lobby = Lobby{};
        lobby.phase = LobbyPhase::Error;
        lobby.error = error;
    }

    bool awake_ = false;
    bool wakeStarted_ = false;
    float wakeDelay_ = kDefaultWakeSeconds;
    float wakeTimer_ = 0.0f;
    float latency_ = kDefaultLatencySeconds;
    Pending pending_ = Pending::None;
    float pendingTimer_ = 0.0f;
    char myName_[kNameMax + 1] = {};
    char pendingCode_[kCodeLength + 1] = {};
    LobbyError nextError_ = LobbyError::None;
};

}  // namespace

Backend& fakeBackend() {
    static FakeBackend fake;
    return fake;
}

}  // namespace ls::net
