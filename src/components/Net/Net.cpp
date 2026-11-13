#include "components/Net/Net.h"

#include "components/Net/Backend.h"

#include <cstring>

namespace ls::net {

namespace {

Lobby g_lobby;
Backend* g_backend = nullptr;  // Null means the default.

// What retry() repeats. Only the last request is kept.
enum class Request { None, Wake, Host, Join, Rejoin };
Request g_lastRequest = Request::None;
char g_lastName[kNameMax + 1] = {};
char g_lastCode[kCodeLength + 1] = {};

// The settings the screen wants the room to have, and the last ones sent to the backend, so they
// go out once and then only when something changes.
struct SettingsWish {
    bool valid = false;
    int map = 0, difficulty = 0, mode = 0;
};
SettingsWish g_settingsWanted;
SettingsWish g_settingsSent;
char g_settingsSentCode[kCodeLength + 1] = {};

Backend& backend() { return g_backend ? *g_backend : defaultBackend(); }

// Sends the wanted settings if this player hosts a room and the server has not heard them yet.
void sendSettingsIfNeeded() {
    const bool hosting = (g_lobby.phase == LobbyPhase::InLobby || g_lobby.phase == LobbyPhase::Starting) && g_lobby.you >= 0 &&
                         g_lobby.you == g_lobby.host;
    if (!hosting) {
        g_settingsSent = SettingsWish{};  // The room is gone, so the next room hears them again.
        g_settingsSentCode[0] = '\0';
        return;
    }
    if (!g_settingsWanted.valid) return;
    const bool same = g_settingsSent.valid && g_settingsSent.map == g_settingsWanted.map &&
                      g_settingsSent.difficulty == g_settingsWanted.difficulty && g_settingsSent.mode == g_settingsWanted.mode &&
                      std::strcmp(g_settingsSentCode, g_lobby.code) == 0;
    if (same) return;
    g_settingsSent = g_settingsWanted;
    std::memcpy(g_settingsSentCode, g_lobby.code, sizeof(g_settingsSentCode));
    backend().setSettings(g_lobby, g_settingsWanted.map, g_settingsWanted.difficulty, g_settingsWanted.mode);
}

bool isLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Copies the name without spaces at either end. Returns false, with an empty result, if
// what is left is not a valid name.
bool trimmedName(const char* in, char (&out)[kNameMax + 1]) {
    out[0] = '\0';
    if (!in) return false;
    const char* begin = in;
    while (*begin == ' ') ++begin;
    const char* end = begin + std::strlen(begin);
    while (end > begin && end[-1] == ' ') --end;
    const std::size_t length = static_cast<std::size_t>(end - begin);
    if (length == 0 || length > static_cast<std::size_t>(kNameMax)) return false;
    std::memcpy(out, begin, length);
    out[length] = '\0';
    return validName(out);
}

// Writes the code in capitals. Returns false if it is not a valid code.
bool upperCode(const char* in, char (&out)[kCodeLength + 1]) {
    out[0] = '\0';
    if (!in || std::strlen(in) != static_cast<std::size_t>(kCodeLength)) return false;
    for (int i = 0; i < kCodeLength; ++i) {
        const char c = in[i];
        out[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    out[kCodeLength] = '\0';
    return validCode(out);
}

// A request that failed before it left this machine. The screens show it like any other error.
void failNow(LobbyError error) {
    g_lobby = Lobby{};
    g_lobby.phase = LobbyPhase::Error;
    g_lobby.error = error;
}

void forget() {
    g_lobby = Lobby{};
    g_settingsSent = SettingsWish{};  // Whatever room comes next has not heard them yet.
    g_settingsSentCode[0] = '\0';
}

}  // namespace

const Lobby& lobby() { return g_lobby; }

const char* transportName(Transport transport) {
    switch (transport) {
        case Transport::Local: return "local";
        case Transport::Pending: return "pending";
        case Transport::WebRtc: return "webrtc";
        case Transport::Relay: return "relay";
        case Transport::None: break;
    }
    return "none";
}

bool canStart() {
    if (g_lobby.phase != LobbyPhase::InLobby || g_lobby.started) return false;
    if (g_lobby.you < 0 || g_lobby.you != g_lobby.host) return false;
    if (g_lobby.count < 2) return false;
    for (int i = 0; i < g_lobby.count; ++i) {
        if (!g_lobby.players[i].ready) return false;
    }
    return true;
}

bool isHere(const LobbyPlayer& player) { return !player.away; }

int hereCount() {
    int here = 0;
    for (int i = 0; i < g_lobby.count; ++i) here += isHere(g_lobby.players[i]) ? 1 : 0;
    return here;
}

const char* rejoinCode() {
    const char* code = backend().rejoinCode();
    return code ? code : "";
}

bool canRejoin() { return rejoinCode()[0] != '\0'; }

bool nameCharAllowed(char c) { return isLetter(c) || isDigit(c) || c == ' '; }

bool codeCharAllowed(char c) {
    const char upper = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    if (upper == '\0') return false;
    return std::strchr(kCodeAlphabet, upper) != nullptr;
}

bool validName(const char* name) {
    if (!name) return false;
    const std::size_t length = std::strlen(name);
    if (length == 0 || length > static_cast<std::size_t>(kNameMax)) return false;
    if (name[0] == ' ' || name[length - 1] == ' ') return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (!nameCharAllowed(name[i])) return false;
    }
    return true;
}

bool validCode(const char* code) {
    if (!code || std::strlen(code) != static_cast<std::size_t>(kCodeLength)) return false;
    for (int i = 0; i < kCodeLength; ++i) {
        if (!codeCharAllowed(code[i])) return false;
    }
    return true;
}

void wakeServer() {
    g_lastRequest = Request::Wake;
    backend().wake(g_lobby);
}

void hostRoom(const char* name) {
    char clean[kNameMax + 1];
    if (!trimmedName(name, clean)) {
        failNow(LobbyError::BadName);
        return;
    }
    g_lastRequest = Request::Host;
    std::memcpy(g_lastName, clean, sizeof(g_lastName));
    forget();
    backend().host(g_lobby, clean);
}

void joinRoom(const char* code, const char* name) {
    char clean[kNameMax + 1];
    if (!trimmedName(name, clean)) {
        failNow(LobbyError::BadName);
        return;
    }
    char upper[kCodeLength + 1];
    if (!upperCode(code, upper)) {
        failNow(LobbyError::NotFound);
        return;
    }
    g_lastRequest = Request::Join;
    std::memcpy(g_lastName, clean, sizeof(g_lastName));
    std::memcpy(g_lastCode, upper, sizeof(g_lastCode));
    forget();
    backend().join(g_lobby, upper, clean);
}

void rejoinRoom() {
    if (!canRejoin()) return;
    g_lastRequest = Request::Rejoin;
    forget();
    backend().rejoin(g_lobby);
}

void forgetRejoin() { backend().forgetRejoin(); }

void abandonRun(LobbyError error) {
    backend().abandon(g_lobby, error);
    g_lastRequest = Request::None;
}

void setReady(bool ready) {
    if (g_lobby.phase != LobbyPhase::InLobby || g_lobby.started) return;
    backend().setReady(g_lobby, ready);
}

void setRoomSettings(int map, int difficulty, int mode) {
    const auto ok = [](int v) { return v >= 0 && v <= kSettingMax; };
    if (!ok(map) || !ok(difficulty) || !ok(mode)) return;
    g_settingsWanted = SettingsWish{true, map, difficulty, mode};
    sendSettingsIfNeeded();
}

void startRun() {
    if (!canStart()) return;
    backend().start(g_lobby);
}

void leaveRoom() {
    backend().leave(g_lobby);
    forget();
    g_lastRequest = Request::None;
}

void retry() {
    const Request request = g_lastRequest;
    char name[kNameMax + 1];
    char code[kCodeLength + 1];
    std::memcpy(name, g_lastName, sizeof(name));
    std::memcpy(code, g_lastCode, sizeof(code));
    switch (request) {
        case Request::Host: hostRoom(name); break;
        case Request::Join: joinRoom(code, name); break;
        case Request::Rejoin:
            if (canRejoin()) {
                rejoinRoom();
                break;
            }
            forget();
            wakeServer();
            break;
        case Request::Wake:
        case Request::None:
            forget();
            wakeServer();
            break;
    }
}

void update(float dt) {
    backend().update(g_lobby, dt);
    // The room may have just come up, so the host's settings go out now, not only on the next change.
    sendSettingsIfNeeded();
}

bool sendGame(int toId, const unsigned char* bytes, int length, bool reliable) {
    if (!bytes || length <= 0) return false;
    return backend().sendGame(toId, bytes, length, reliable);
}

int receiveGame(unsigned char* buffer, int capacity, int* fromId) {
    if (!buffer || capacity <= 0) return 0;
    return backend().receiveGame(buffer, capacity, fromId);
}

void debugCommand(int op, int value) {
    if (op == kDebugPhase) {
        g_lobby.phase = static_cast<LobbyPhase>(value);
        return;
    }
    if (op == kDebugWakeSeconds) {
        g_lobby.wakeSeconds = static_cast<float>(value);
        return;
    }
    backend().debug(g_lobby, op, value);
}

void setBackend(Backend* backend) { g_backend = backend; }

}  // namespace ls::net
