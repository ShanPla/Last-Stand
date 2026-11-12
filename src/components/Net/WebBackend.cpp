#include "components/Net/WebBackend.h"

#include "components/Net/Backend.h"

#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// The real lobby backend for the browser. The networking itself (the WebSocket to the relay,
// WebRTC, the wake-up request) lives in JavaScript, in web/components/NetClient. This file is
// the thin bridge: it tells that script what the player pressed, and every frame it copies
// what the script knows into the Lobby that the screens draw.
//
// Why copy instead of calling back: browser events never run inside a game frame, so the game
// asks the script for its state between frames. There is nothing to lock and nothing that can
// change the lobby while the screens are drawing it.
//
// No memory is allocated here after startup. The script's answer is written into a fixed
// buffer and read with a small scanner, because the few fields we need do not justify a full
// JSON library.
namespace ls::net {

// ---- Reading the script's JSON ---------------------------------------------------

namespace {

struct Span {
    const char* begin;
    const char* end;
};

// Finds "key": inside the span and returns where its value starts, or null. The script's JSON
// has no spaces, and a key is always inside quotes followed by a colon, so this is exact.
const char* findKey(Span span, const char* key) {
    const std::size_t length = std::strlen(key);
    for (const char* p = span.begin; p + length + 3 <= span.end; ++p) {
        if (p[0] == '"' && std::memcmp(p + 1, key, length) == 0 && p[1 + length] == '"' && p[2 + length] == ':') return p + 3 + length;
    }
    return nullptr;
}

int readInt(const char* value, int fallback) {
    if (!value) return fallback;
    bool negative = false;
    if (*value == '-') {
        negative = true;
        ++value;
    }
    if (*value < '0' || *value > '9') return fallback;
    long number = 0;
    while (*value >= '0' && *value <= '9') {
        number = number * 10 + (*value - '0');
        if (number > 1000000000L) number = 1000000000L;
        ++value;
    }
    return static_cast<int>(negative ? -number : number);
}

bool readBool(const char* value) { return value && *value == 't'; }

// Copies a JSON string. Characters that cannot be in a name or a code become '?', and a
// string that is too long is cut. Returns the length written.
std::size_t readString(const char* value, char* out, std::size_t capacity) {
    std::size_t length = 0;
    if (capacity == 0) return 0;
    out[0] = '\0';
    if (!value || *value != '"') return 0;
    for (const char* p = value + 1; *p && *p != '"'; ++p) {
        char c = *p;
        if (c == '\\' && p[1]) c = *++p;  // An escaped character is taken as written. The server never sends escapes in names.
        if (length + 1 < capacity) out[length++] = (c >= 32 && c < 127) ? c : '?';
    }
    out[length] = '\0';
    return length;
}

bool stringIs(const char* value, const char* text) {
    if (!value || *value != '"') return false;
    const std::size_t length = std::strlen(text);
    return std::strncmp(value + 1, text, length) == 0 && value[1 + length] == '"';
}

// p is at an opening quote. Returns the position after the closing one.
const char* skipString(const char* p, const char* end) {
    for (++p; p < end && *p != '"'; ++p) {
        if (*p == '\\') ++p;
    }
    return p < end ? p + 1 : end;
}

// open is at a '{' or '['. Returns the matching closing character, or null when the text ends first.
const char* matchClose(const char* open, const char* end) {
    int depth = 0;
    for (const char* p = open; p < end; ++p) {
        if (*p == '"') {
            p = skipString(p, end) - 1;
        } else if (*p == '{' || *p == '[') {
            ++depth;
        } else if (*p == '}' || *p == ']') {
            if (--depth == 0) return p;
        }
    }
    return nullptr;
}

// The script numbers its phases and errors in a fixed order. They are listed by name here so a
// change in either list shows up as a compile-time-visible edit instead of a silent shift.
LobbyPhase phaseFrom(int index, bool wakeRunning) {
    switch (index) {
        case 1: return LobbyPhase::Waking;
        // Pressing Host while the server is still asleep is Connecting to the script, but the
        // screens call that Waking, because it is the wait the player is looking at.
        case 2: return wakeRunning ? LobbyPhase::Waking : LobbyPhase::Connecting;
        case 3: return LobbyPhase::InLobby;
        case 4: return LobbyPhase::Starting;
        case 5: return LobbyPhase::Error;
        case 6: return LobbyPhase::Reconnecting;
        default: return LobbyPhase::Offline;
    }
}

LobbyError errorFrom(int index) {
    switch (index) {
        case 0: return LobbyError::ServerUnreachable;
        case 1: return LobbyError::NotFound;
        case 2: return LobbyError::Full;
        case 3: return LobbyError::Started;
        case 4: return LobbyError::BadName;
        case 5: return LobbyError::HostLeft;
        case 6: return LobbyError::Version;
        case 8: return LobbyError::RunEnded;
        case 9: return LobbyError::RoomClosed;
        case 10: return LobbyError::Replaced;
        default: return LobbyError::Other;
    }
}

// The other way, for abandon(): the script's index of a C++ error.
int errorIndexOf(LobbyError error) {
    switch (error) {
        case LobbyError::ServerUnreachable: return 0;
        case LobbyError::NotFound: return 1;
        case LobbyError::Full: return 2;
        case LobbyError::Started: return 3;
        case LobbyError::BadName: return 4;
        case LobbyError::HostLeft: return 5;
        case LobbyError::Version: return 6;
        case LobbyError::RunEnded: return 8;
        case LobbyError::RoomClosed: return 9;
        case LobbyError::Replaced: return 10;
        case LobbyError::Other:
        case LobbyError::None: break;
    }
    return 7;
}

Transport transportFrom(const char* value) {
    if (stringIs(value, "local")) return Transport::Local;
    if (stringIs(value, "pending")) return Transport::Pending;
    if (stringIs(value, "webrtc")) return Transport::WebRtc;
    if (stringIs(value, "relay")) return Transport::Relay;
    return Transport::None;
}

void readName(const char* value, char (&out)[kNameMax + 1]) {
    readString(value, out, sizeof(out));
    for (char* c = out; *c; ++c) {
        if (!nameCharAllowed(*c)) *c = '?';
    }
}

}  // namespace

bool applyWebSnapshot(const char* json, Lobby& lobby) {
    if (!json) return false;
    const Span whole{json, json + std::strlen(json)};
    const char* phaseValue = findKey(whole, "phaseIndex");
    const char* playersValue = findKey(whole, "players");
    if (!phaseValue || !playersValue || *playersValue != '[') return false;
    const char* playersEnd = matchClose(playersValue, whole.end);
    if (!playersEnd) return false;

    // Keys before the player list belong to the room. The list's own objects repeat a few
    // names (such as "ready"), so each part is searched on its own.
    const Span room{whole.begin, playersValue};
    const Span after{playersEnd + 1, whole.end};

    Lobby next;
    next.wakeSeconds = lobby.wakeSeconds;
    const bool wakeRunning = stringIs(findKey(room, "state"), "waking");
    next.phase = phaseFrom(readInt(phaseValue, 0), wakeRunning);
    if (next.phase == LobbyPhase::Error) next.error = errorFrom(readInt(findKey(room, "errorIndex"), 7));
    readString(findKey(room, "code"), next.code, sizeof(next.code));
    next.started = readBool(findKey(room, "started"));
    next.serverRttMs = readInt(findKey(after, "serverRttMs"), -1);

    const int myId = readInt(findKey(room, "myId"), -1);
    const int hostId = readInt(findKey(room, "hostId"), -1);

    // Each object of the list is one player.
    const char* p = playersValue + 1;
    while (p < playersEnd && next.count < kMaxPlayers) {
        while (p < playersEnd && *p != '{') ++p;
        if (p >= playersEnd) break;
        const char* objectEnd = matchClose(p, playersEnd + 1);
        if (!objectEnd) break;
        const Span object{p, objectEnd};
        const int id = readInt(findKey(object, "id"), -1);
        if (id >= 0 && id < kMaxPlayers) {
            LobbyPlayer& player = next.players[next.count];
            player.id = id;
            readName(findKey(object, "name"), player.name);
            player.ready = readBool(findKey(object, "ready"));
            player.transport = transportFrom(findKey(object, "transport"));
            player.rttMs = readInt(findKey(object, "rttMs"), -1);
            player.away = readBool(findKey(object, "away"));
            player.returns = readInt(findKey(object, "returns"), 0);
            if (id == myId) next.you = next.count;
            if (id == hostId) next.host = next.count;
            ++next.count;
        }
        p = objectEnd + 1;
    }
    lobby = next;
    return true;
}

void readWebRejoin(const char* json, char (&code)[kCodeLength + 1]) {
    code[0] = '\0';
    if (!json) return;
    const Span whole{json, json + std::strlen(json)};
    const char* playersValue = findKey(whole, "players");
    const char* playersEnd = playersValue && *playersValue == '[' ? matchClose(playersValue, whole.end) : nullptr;
    // The key comes after the player list, so a player's own keys can never be mistaken for it.
    const Span after{playersEnd ? playersEnd + 1 : whole.begin, whole.end};
    char text[kCodeLength + 1] = {};
    readString(findKey(after, "rejoin"), text, sizeof(text));
    if (validCode(text)) std::memcpy(code, text, sizeof(text));
}

void applyWebStats(const char* json, Lobby& lobby) {
    if (!json) return;
    const Span whole{json, json + std::strlen(json)};
    const char* peersValue = findKey(whole, "peers");
    if (!peersValue || *peersValue != '{') return;
    const char* peersEnd = matchClose(peersValue, whole.end);
    if (!peersEnd) return;
    const Span peers{peersValue, peersEnd};
    for (int i = 0; i < lobby.count; ++i) {
        LobbyPlayer& player = lobby.players[i];
        player.msgsInPerSec = 0;
        player.msgsOutPerSec = 0;
        char key[8];
        std::snprintf(key, sizeof(key), "%d", player.id);
        const char* object = findKey(peers, key);
        if (!object || *object != '{') continue;
        const char* objectEnd = matchClose(object, peersEnd + 1);
        if (!objectEnd) continue;
        const Span peer{object, objectEnd};
        player.msgsInPerSec = readInt(findKey(peer, "msgsInPerSec"), 0);
        player.msgsOutPerSec = readInt(findKey(peer, "msgsOutPerSec"), 0);
    }
}

// ---- The bridge to the script ----------------------------------------------------

#ifdef __EMSCRIPTEN__

}  // namespace ls::net

// These small functions are compiled into the page's JavaScript. Every one of them asks the
// shared client from NetClient.js, and returns -1 when that script is not on the page, so a
// missing file turns into a "could not reach the server" instead of a crash.

EM_JS(int, ls_js_net_use_fake, (), {
    try { return new URLSearchParams(location.search).has('fakenet') ? 1 : 0; } catch (error) { return 0; }
});

// Starts waking the server, unless the page is a local one with no server named: a server on
// this machine is never asleep, and there may be nothing listening.
EM_JS(int, ls_js_net_wake, (), {
    try {
        if (typeof LastStandNet === 'undefined') return -1;
        var net = LastStandNet.shared();
        if (net.config.wakeOnLoad === false) return 0;
        net.wake();
        return 1;
    } catch (error) { return -1; }
});

// Host and Join both cancel whatever the script was doing first. Without that, pressing Retry
// while the script still waited on an old attempt would be ignored.
EM_JS(int, ls_js_net_host, (const char* name), {
    try {
        if (typeof LastStandNet === 'undefined') return -1;
        var text = '';
        for (var i = 0; HEAPU8[name + i] !== 0; i++) text += String.fromCharCode(HEAPU8[name + i]);
        var net = LastStandNet.shared();
        net.leave();
        net.create(text);
        return 1;
    } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_join, (const char* code, const char* name), {
    try {
        if (typeof LastStandNet === 'undefined') return -1;
        var codeText = '';
        for (var i = 0; HEAPU8[code + i] !== 0; i++) codeText += String.fromCharCode(HEAPU8[code + i]);
        var nameText = '';
        for (var j = 0; HEAPU8[name + j] !== 0; j++) nameText += String.fromCharCode(HEAPU8[name + j]);
        var net = LastStandNet.shared();
        net.leave();
        net.join(codeText, nameText);
        return 1;
    } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_rejoin, (), {
    try {
        if (typeof LastStandNet === 'undefined') return -1;
        var net = LastStandNet.shared();
        if (!net.canRejoin()) return 0;
        net.leave();
        net.rejoin();
        return 1;
    } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_forget_rejoin, (), {
    try { LastStandNet.shared().forgetRejoin(); return 1; } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_abandon, (int errorIndex), {
    try {
        var net = LastStandNet.shared();
        net.abandon(net.ERRORS[errorIndex] || 'RunEnded');
        return 1;
    } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_ready, (int on), {
    try { return LastStandNet.shared().setReady(on !== 0) ? 1 : 0; } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_start, (), {
    try { return LastStandNet.shared().start() ? 1 : 0; } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_leave, (), {
    try { LastStandNet.shared().leave(); return 1; } catch (error) { return -1; }
});

// Takes the events the script has collected since the last frame. Only their length matters
// here: more than the 2 characters of an empty list means something changed, and the frame
// reads the whole snapshot. Returns -1 when the script is missing.
EM_JS(int, ls_js_net_events, (), {
    try { return LastStandNet.shared().takeEventsJson().length; } catch (error) { return -1; }
});

// The next two write the script's text into a buffer the game owns. Names and codes are plain
// ASCII on the server, and anything else is written as '?'. A text that does not fit gives -2.
EM_JS(int, ls_js_net_snapshot, (char* buffer, int capacity), {
    try {
        var text = LastStandNet.shared().snapshotJson();
        if (text.length + 1 > capacity) return -2;
        for (var i = 0; i < text.length; i++) {
            var c = text.charCodeAt(i);
            HEAPU8[buffer + i] = c < 128 ? c : 63;
        }
        HEAPU8[buffer + text.length] = 0;
        return text.length;
    } catch (error) { return -1; }
});

EM_JS(int, ls_js_net_stats, (char* buffer, int capacity), {
    try {
        var text = LastStandNet.shared().statsJson();
        if (text.length + 1 > capacity) return -2;
        for (var i = 0; i < text.length; i++) {
            var c = text.charCodeAt(i);
            HEAPU8[buffer + i] = c < 128 ? c : 63;
        }
        HEAPU8[buffer + text.length] = 0;
        return text.length;
    } catch (error) { return -1; }
});

// Game messages. The script hands each one to onMessage as it arrives, between frames. This
// keeps them in a queue that the game empties at the start of its next frame, so a message
// never arrives in the middle of a frame. The queue lives in JavaScript, so the game itself
// allocates nothing for it. It also tells the game when the page is hidden, because a hidden
// tab may run no frame at all until it comes back.
EM_JS(void, ls_js_net_listen, (), {
    try {
        if (typeof LastStandNet === "undefined") return;
        var net = LastStandNet.shared();
        if (net.lsGameQueue) return;
        net.lsGameQueue = [];
        net.onMessage = function (from, bytes) {
            var queue = net.lsGameQueue;
            if (queue.length >= 1024) {
                // Full (a hidden tab runs no frames to empty it). A snapshot or an input is
                // dropped first, because a newer one replaces it. The rare reliable messages
                // (settings, card picks, the end of the run) stay.
                var drop = 0;
                for (var k = 0; k < queue.length; k++) {
                    var kind = queue[k][1][0];
                    if (kind === 2 || kind === 3) { drop = k; break; }
                }
                queue.splice(drop, 1);
            }
            queue.push([from, bytes.slice()]);
        };
        document.addEventListener("visibilitychange", function () {
            try { if (Module._ls_net_page_hidden) Module._ls_net_page_hidden(document.hidden ? 1 : 0); } catch (error) {}
        });
    } catch (error) {}
});

EM_JS(int, ls_js_net_send, (int to, const unsigned char* bytes, int length, int reliable), {
    try { return LastStandNet.shared().send(to, HEAPU8.subarray(bytes, bytes + length), reliable !== 0) ? 1 : 0; } catch (error) { return -1; }
});

// Returns the length of the next message, 0 when none waits, or -2 for one too big (dropped).
EM_JS(int, ls_js_net_receive, (unsigned char* buffer, int capacity, int* from), {
    try {
        var queue = LastStandNet.shared().lsGameQueue;
        if (!queue || queue.length === 0) return 0;
        var item = queue.shift();
        if (item[1].length > capacity) return -2;
        HEAPU8.set(item[1], buffer);
        HEAP32[from >> 2] = item[0];
        return item[1].length;
    } catch (error) { return -1; }
});

namespace ls::net {

namespace {

constexpr float kHeartbeatSeconds = 0.25f;  // The longest the lobby goes without being re-read.
constexpr float kReadyWaitSeconds = 2.0f;   // How long a ready press is shown before the server confirms it.

class WebBackend final : public Backend {
public:
    void wake(Lobby& lobby) override {
        // A player already in a room has no use for a wake.
        if (lobby.phase != LobbyPhase::Offline) return;
        if (ls_js_net_wake() > 0) pull(lobby);
    }

    void host(Lobby& lobby, const char* name) override {
        readyWanted_ = -1;
        if (ls_js_net_host(name) < 0) {
            unavailable(lobby);
            return;
        }
        pull(lobby);
    }

    void join(Lobby& lobby, const char* code, const char* name) override {
        readyWanted_ = -1;
        if (ls_js_net_join(code, name) < 0) {
            unavailable(lobby);
            return;
        }
        pull(lobby);
    }

    void setReady(Lobby& lobby, bool ready) override {
        ls_js_net_ready(ready ? 1 : 0);
        // The server's answer takes a round trip. Until it comes, show the press, so the button
        // does not jump back for a moment.
        readyWanted_ = ready ? 1 : 0;
        readyAge_ = 0.0f;
        applyWantedReady(lobby);
    }

    void start(Lobby&) override {
        // The lobby moves to Starting when the server's answer comes back, for every player alike.
        ls_js_net_start();
        dirty_ = true;
    }

    void leave(Lobby&) override {
        readyWanted_ = -1;
        ls_js_net_leave();
    }

    void rejoin(Lobby& lobby) override {
        readyWanted_ = -1;
        if (ls_js_net_rejoin() < 0) {
            unavailable(lobby);
            return;
        }
        pull(lobby);
    }

    const char* rejoinCode() const override { return rejoinCode_; }

    void forgetRejoin() override {
        ls_js_net_forget_rejoin();
        rejoinCode_[0] = '\0';
        dirty_ = true;
    }

    void abandon(Lobby& lobby, LobbyError error) override {
        readyWanted_ = -1;
        ls_js_net_abandon(errorIndexOf(error));
        rejoinCode_[0] = '\0';
        pull(lobby);
    }

    bool sendGame(int toId, const unsigned char* bytes, int length, bool reliable) override {
        return ls_js_net_send(toId, bytes, length, reliable ? 1 : 0) > 0;
    }

    int receiveGame(unsigned char* buffer, int capacity, int* fromId) override {
        int from = -1;
        for (;;) {
            const int length = ls_js_net_receive(buffer, capacity, &from);
            if (length == -2) continue;  // Too big to be ours. The next one may be fine.
            if (length <= 0) return 0;
            if (fromId) *fromId = from;
            return length;
        }
    }

    void update(Lobby& lobby, float dt) override {
        ls_js_net_listen();
        heartbeat_ += dt;
        const int events = ls_js_net_events();
        if (events < 0) return;
        if (events > 2 || dirty_ || heartbeat_ >= kHeartbeatSeconds) pull(lobby);
        if (lobby.phase == LobbyPhase::Waking) lobby.wakeSeconds += dt;
        if (readyWanted_ >= 0) {
            readyAge_ += dt;
            if (lobby.phase != LobbyPhase::InLobby || readyAge_ > kReadyWaitSeconds) readyWanted_ = -1;
        }
    }

private:
    // The script is not on the page. The screens show their usual "could not reach the server".
    void unavailable(Lobby& lobby) {
        lobby = Lobby{};
        lobby.phase = LobbyPhase::Error;
        lobby.error = LobbyError::ServerUnreachable;
    }

    // Reads the script's state into the lobby.
    void pull(Lobby& lobby) {
        heartbeat_ = 0.0f;
        dirty_ = false;
        const LobbyPhase before = lobby.phase;
        if (ls_js_net_snapshot(snapshot_, sizeof(snapshot_)) < 0) return;
        if (!applyWebSnapshot(snapshot_, lobby)) return;
        readWebRejoin(snapshot_, rejoinCode_);
        if (ls_js_net_stats(stats_, sizeof(stats_)) >= 0) applyWebStats(stats_, lobby);
        // The Retry timer counts from the moment the wait began.
        if (lobby.phase != LobbyPhase::Waking || before != LobbyPhase::Waking) lobby.wakeSeconds = 0.0f;
        applyWantedReady(lobby);
    }

    // Keeps a ready press on screen until the server's room message agrees with it.
    void applyWantedReady(Lobby& lobby) {
        if (readyWanted_ < 0 || lobby.you < 0 || lobby.you >= lobby.count) return;
        bool& ready = lobby.players[lobby.you].ready;
        if (ready == (readyWanted_ == 1)) {
            readyWanted_ = -1;
        } else {
            ready = readyWanted_ == 1;
        }
    }

    float heartbeat_ = 0.0f;
    bool dirty_ = false;
    int readyWanted_ = -1;  // -1: no press waiting for the server.
    float readyAge_ = 0.0f;
    char snapshot_[4096] = {};
    char rejoinCode_[kCodeLength + 1] = {};  // The saved seat's room, copied from the script.
    char stats_[1024] = {};
};

}  // namespace

Backend& defaultBackend() {
    static const bool useFake = ls_js_net_use_fake() != 0;
    if (useFake) return fakeBackend();
    static WebBackend web;
    return web;
}

#else

Backend& defaultBackend() { return fakeBackend(); }

#endif

}  // namespace ls::net
