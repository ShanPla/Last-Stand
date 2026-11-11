#pragma once

#include "components/Net/Net.h"

// The seam between the lobby logic in Net.cpp and whatever really talks to a server.
//
// Net.cpp owns the Lobby and checks every request (names, codes, who may start) before it
// reaches a backend. A backend only has to carry the request out and update the Lobby it is
// given as answers arrive. The fake backend (FakeBackend.cpp) answers from a timer. The real
// one will send the protocol's JSON messages from the browser and fill the Lobby in from the
// server's `room`, `started`, `closed` and `error` messages.
namespace ls::net {

class Backend {
public:
    virtual ~Backend() = default;

    // Start waking the server. The lobby goes Waking while waiting, and back to Offline when
    // the server answers.
    virtual void wake(Lobby& lobby) = 0;
    // Create a room, or join one. The name and code are already checked. A server that is
    // still asleep means Waking first, then Connecting, then InLobby, or Error.
    virtual void host(Lobby& lobby, const char* name) = 0;
    virtual void join(Lobby& lobby, const char* code, const char* name) = 0;
    virtual void setReady(Lobby& lobby, bool ready) = 0;
    virtual void start(Lobby& lobby) = 0;
    virtual void leave(Lobby& lobby) = 0;
    // Advances timers and reads answers. Called every frame.
    virtual void update(Lobby& lobby, float dt) = 0;
    // For the fake only. A real backend ignores it.
    virtual void debug(Lobby& lobby, int op, int value) { (void)lobby; (void)op; (void)value; }
};

// The fake. It is what Net uses until setBackend() is called with another.
Backend& fakeBackend();

// Swaps the backend. Null puts the fake back. Used by the browser layer, and by tests.
void setBackend(Backend* backend);

}  // namespace ls::net
