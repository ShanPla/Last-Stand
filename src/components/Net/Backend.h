#pragma once

#include "components/Net/Net.h"

// The seam between the lobby logic in Net.cpp and whatever really talks to a server.
//
// Net.cpp owns the Lobby and checks every request (names, codes, who may start) before it
// reaches a backend. A backend only has to carry the request out and update the Lobby it is
// given as answers arrive. The fake backend (FakeBackend.cpp) answers from a timer. The real
// one (WebBackend.cpp) asks the browser's networking script to talk to the relay, and copies
// what that script knows into the Lobby every frame.
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
    // Game messages once the run has started. See sendGame and receiveGame in Net.h.
    virtual bool sendGame(int toId, const unsigned char* bytes, int length, bool reliable) {
        (void)toId; (void)bytes; (void)length; (void)reliable;
        return false;
    }
    virtual int receiveGame(unsigned char* buffer, int capacity, int* fromId) {
        (void)buffer; (void)capacity; (void)fromId;
        return 0;
    }
    // For the fake only. A real backend ignores it.
    virtual void debug(Lobby& lobby, int op, int value) { (void)lobby; (void)op; (void)value; }
};

// The fake.
Backend& fakeBackend();

// What Net uses until setBackend() is called with another: the real one in the browser, and
// the fake in the browser with ?fakenet in the address, and in every native build.
Backend& defaultBackend();

// Swaps the backend. Null puts the default back. Used by tests.
void setBackend(Backend* backend);

}  // namespace ls::net
