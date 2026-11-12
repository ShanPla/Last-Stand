#pragma once

// The co-op lobby, as the screens see it. The screens only ever read lobby() and call the
// actions below, so they never know whether a real server or a fake one is behind them.
//
// This file has no raylib and no browser code, so unit tests can run it. The work itself is
// done by a Backend (see Backend.h). In the browser the backend talks to the relay server
// (WebBackend.cpp). Everywhere else, and in the browser with ?fakenet in the address, it is a
// fake that pretends to be a server.
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
    Starting,    // The host pressed Start. The run is about to begin, or is going on.
    Error,       // Something failed. Lobby::error says what.
    // A guest's connection dropped during a run, and it is getting its seat back by itself.
    // The run stays on screen meanwhile. It comes last so the numbers above never change.
    Reconnecting,
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
    RunEnded,           // A rejoin was refused: the run this player was in is over for them.
    RoomClosed,         // A rejoin found no room: the host left, or the room expired.
    Replaced,           // The same player took their seat back from another tab.
};

// How game messages travel between this player and another one. Only the web backend knows.
enum class Transport {
    None,     // Not known, or not in a room.
    Local,    // This player.
    Pending,  // A direct connection is being tried.
    WebRtc,   // Direct from browser to browser.
    Relay,    // Through the server, because the direct connection did not work.
};
const char* transportName(Transport transport);

struct LobbyPlayer {
    int id = -1;            // The server's slot, 0 to 3. The host is always slot 0.
    char name[kNameMax + 1] = {};
    bool ready = false;
    // In a started run, a player whose connection is gone keeps their seat and is listed as
    // away, because they may come back. An away player has no connection to send to.
    bool away = false;
    // How many times this player has come back from away. It only goes up, so a return is seen
    // even when the player left and came back between two frames.
    int returns = 0;
    // Numbers for the debug overlay. They stay at these values with the fake backend.
    Transport transport = Transport::None;
    int rttMs = -1;          // Round trip to this player in milliseconds, or -1 when not measured.
    int msgsInPerSec = 0;
    int msgsOutPerSec = 0;
};

struct Lobby {
    LobbyPhase phase = LobbyPhase::Offline;
    LobbyError error = LobbyError::None;
    char code[kCodeLength + 1] = {};  // The room code, or empty outside a room.
    // you and host are positions in players[], which the screens use as rows. players are listed
    // with no gaps, so when someone leaves, the server's slots (players[i].id) can skip a number
    // while the positions do not.
    int you = -1;                     // This player's position in players, or -1 outside a room.
    int host = -1;                    // The host's position in players, or -1 outside a room.
    bool started = false;             // The host has started the run.
    int count = 0;                    // How many entries of players are in use.
    LobbyPlayer players[kMaxPlayers];
    float wakeSeconds = 0.0f;         // How long the server has been waking, for the Retry timeout.
    int serverRttMs = -1;             // Round trip to the server in milliseconds, or -1.
};

// ---- Reading ---------------------------------------------------------------------

const Lobby& lobby();

// True when this player is the host, in a room that has not started, with at least two
// players in it and every player ready. The Start button and startRun() both use this.
bool canStart();

// The players in the room who are connected right now, that is, not away.
int hereCount();
bool isHere(const LobbyPlayer& player);

// The room code of a run this player can take their seat back in (after a reload or a closed
// tab), or an empty string when there is none. The browser keeps the seat; natively only the
// fake backend can pretend to have one.
const char* rejoinCode();
bool canRejoin();

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

// Takes back the seat of the run in rejoinCode(). Waking, then Connecting, then Starting like a
// join, or Error with RunEnded or RoomClosed (and then the saved seat is gone).
void rejoinRoom();

// Forgets the saved seat. Call when the player leaves a run for good.
void forgetRejoin();

// Leaves the room, forgets the saved seat, and shows this error. For when the game learns the
// run is over for this player, such as the host starting a new run without them.
void abandonRun(LobbyError error);

void setReady(bool ready);
void startRun();   // Host only, and only when canStart() says so. Otherwise nothing happens.
void leaveRoom();  // Back to Offline. Also clears an error.
void retry();      // Repeats the last host, join, rejoin or wake that failed or timed out.

// Call once per frame, with the time since the last frame, so timers and the backend advance.
void update(float dt);

// ---- Game messages, during the run ------------------------------------------------

// Sends bytes to one player, by their server slot (LobbyPlayer::id), or to everyone with 255.
// reliable picks the ordered channel that resends what is lost. Returns false when nothing could
// be sent, which is always the case with the fake backend.
bool sendGame(int toId, const unsigned char* bytes, int length, bool reliable);
// Copies the next message that arrived into buffer and returns its length, or 0 when none is
// waiting. fromId gets the sender's server slot. A message too big for the buffer is dropped.
int receiveGame(unsigned char* buffer, int capacity, int* fromId);

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
    kDebugRejoinSeat = 111,   // 1: the fake keeps a seat to rejoin in room K7QXM. 0: none.
    kDebugAwayMask = 112,     // Bit n set: player n of the fake room is away.
};
void debugCommand(int op, int value);

}  // namespace ls::net
