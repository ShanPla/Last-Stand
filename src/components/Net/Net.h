#pragma once

// The co-op lobby, as the screens see it. The screens only ever read lobby() and call the
// actions below, so they never know whether a real server or a fake one is behind them.
//
// This file has no raylib and no browser code, so unit tests can run it. The work itself is
// done by a Backend (see Backend.h). For now the backend is a fake that pretends to be a
// server. The browser layer will later swap in one that talks to the relay over a WebSocket.
namespace ls::net {

inline constexpr int kMaxPlayers = 4;
inline constexpr int kNameMax = 12;                       // Characters in a name, as the protocol allows.
inline constexpr int kCodeLength = 5;                     // Characters in a room code.
inline constexpr char kCodeAlphabet[] = "ABCDEFGHJKMNPQRSTUVWXY23456789";  // No 0, O, 1, I, L or Z: the pixel font draws Z like 2.
// A free server can take about a minute to wake. After this long the screens offer Retry.
inline constexpr float kWakeRetrySeconds = 70.0f;

enum class LobbyPhase {
    Offline,     // Not in a room. The server is asleep, or awake and idle.
    Waking,      // Waiting for the server to answer. Can take a minute on a free host.
    Connecting,  // The server is awake. Waiting for it to accept a host or join request.
    InLobby,     // In a room. The player list is live.
    Starting,    // The host pressed Start. The run is about to begin.
    Error,       // Something failed. Lobby::error says what.
};

enum class LobbyError {
    None,
    ServerUnreachable,  // Could not reach the server, or the connection dropped.
    NotFound,           // No room has that code.
    Full,               // The room has 4 players already.
    Started,            // The run in that room has already begun.
    BadName,            // The server (or this code) refused the name.
    HostLeft,           // The host left, so the room is gone.
    Version,            // This page and the server speak different protocol versions.
    Other,              // Anything else, such as a full server or a rate limit.
};

struct LobbyPlayer {
    int id = -1;            // The slot, 0 to 3. The host is always slot 0.
    char name[kNameMax + 1] = {};
    bool ready = false;
};

struct Lobby {
    LobbyPhase phase = LobbyPhase::Offline;
    LobbyError error = LobbyError::None;
    char code[kCodeLength + 1] = {};  // The room code, or empty outside a room.
    int you = -1;                     // This player's slot, or -1 outside a room.
    int host = -1;                    // The host's slot, or -1 outside a room.
    bool started = false;             // The host has started the run.
    int count = 0;                    // How many entries of players are in use.
    LobbyPlayer players[kMaxPlayers];
    float wakeSeconds = 0.0f;         // How long the server has been waking, for the Retry timeout.
};

// ---- Reading ---------------------------------------------------------------------

const Lobby& lobby();

// True when this player is the host, in a room that has not started, with at least two
// players in it and every player ready. The Start button and startRun() both use this.
bool canStart();

// ---- Rules the screens and the server share --------------------------------------

// 1 to 12 letters, digits and spaces, with no space at either end.
bool validName(const char* name);
// 5 characters from kCodeAlphabet, in either case.
bool validCode(const char* code);
// Whether one character may be typed into a name or a code. The text field uses these so that
// bad characters never reach the buffer.
bool nameCharAllowed(char c);
bool codeCharAllowed(char c);  // Accepts either case.

// ---- Actions ---------------------------------------------------------------------

// Wakes the server up early, so a later Host or Join does not have to wait for it. Safe to
// call again. Call it when the title screen first appears.
void wakeServer();

// Make a room and become its host. The name is trimmed first, and a bad one fails at once
// with LobbyError::BadName, without touching the server.
void hostRoom(const char* name);

// Join a room. The code may be in any case. A code that cannot be a code is NotFound.
void joinRoom(const char* code, const char* name);

void setReady(bool ready);
void startRun();   // Host only, and only when canStart() says so. Otherwise nothing happens.
void leaveRoom();  // Back to Offline. Also clears an error.
void retry();      // Repeats the last host, join or wake that failed or timed out.

// Call once per frame, with the time since the last frame, so timers and the backend advance.
void update(float dt);

// ---- Tests and debug builds ------------------------------------------------------

// Commands for the fake backend, so every phase and every error can be shown without a
// server. Browser tests send them through the game's debug command, which hands every command
// from 100 up to debugCommand(). The numbers start at 100 to keep clear of the game's own.
enum DebugOp {
    kDebugReset = 100,        // Fake server asleep again, lobby Offline.
    kDebugWakeDelayMs = 101,  // How long the fake server takes to wake. Negative: never wakes.
    kDebugLatencyMs = 102,    // How long the fake server takes to answer host and join.
    kDebugNextError = 103,    // The next host or join fails with this LobbyError (as a number).
    kDebugRoom = 104,         // A room with this many players (1 to 4). You are the host.
    kDebugGuestRoom = 105,    // The same, but you are slot 1 and the host is slot 0.
    kDebugReadyMask = 106,    // Bit n set: player n is ready. Bit 0 is the host.
    kDebugHostLeft = 107,     // The host leaves: phase Error, error HostLeft.
    kDebugLoseServer = 108,   // The connection drops: phase Error, error ServerUnreachable.
    kDebugWakeSeconds = 109,  // Sets Lobby::wakeSeconds, to reach the Retry timeout quickly.
    kDebugPhase = 110,        // Forces a phase (as a number), keeping the rest of the lobby.
};
void debugCommand(int op, int value);

}  // namespace ls::net
