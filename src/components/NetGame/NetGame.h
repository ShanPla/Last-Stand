#pragma once

#include "components/NetGame/Codec/Codec.h"
#include "core/GameState.h"
#include "core/Sim.h"

#include <cstdint>

// Online co-op during the run itself. The host's game is the only real simulation: it reads
// the other players' inputs from the network, steps the same sim::step that single-player
// uses, and sends each player a snapshot of the world around them about 20 times a second.
// The other players (clients) never step the simulation. Their GameState is only a picture,
// filled from snapshots, except for their own soldier, whose movement they predict with the
// same movement code so their own controls never wait for the network.
//
// Like the simulation, this has no raylib, no clock and no heap: the caller passes the frame
// time, and every buffer is a fixed array. The network itself is behind Link, so unit tests
// can join a host and a client with a pipe in memory.
namespace ls::netgame {

enum class Role { None, Host, Client };

// How messages leave and arrive. Ids are lobby (network) slots, not player slots.
class Link {
public:
    virtual ~Link() = default;
    virtual bool send(int toNetId, const uint8_t* data, int size, bool reliable) = 0;
    // Copies the next waiting message into buffer and returns its size, or 0 when none waits.
    virtual int receive(uint8_t* buffer, int capacity, int* fromNetId) = 0;
};

// The link the game uses: the browser networking from components/Net.
Link& browserLink();

// Numbers for the debug overlay and the tests. Rates are per second, over the last second.
struct Stats {
    int snapshotsOutPerSec = 0;
    int snapshotsInPerSec = 0;
    int messagesOutPerSec = 0;
    int messagesInPerSec = 0;
    int bytesOutPerSec = 0;
    int bytesInPerSec = 0;
    int lastSnapshotBytes = 0;
    int maxSnapshotBytes = 0;
    int avgSnapshotBytes = 0;
    int inputsInPerSec = 0;
    float snapshotAge = 0.0f;     // Client: seconds since the last snapshot arrived.
    float predictionError = 0.0f;  // Client: size of the last correction, in world units.
    int corrections = 0;           // Client: snapshots that moved the prediction.
    int snaps = 0;                 // Client: corrections too big to smooth.
    float drawnOffset = 0.0f;      // Client: how far the drawn soldier is from the prediction now.
    int lateSteps = 0;             // Host: steps a player stood still because their input was late.
    int droppedSteps = 0;          // Host: input steps left out because too many waited.
};

// ---- For everyone --------------------------------------------------------------

Role role();
const RunStart& settings();           // The current run's settings. Only valid during a co-op run.
int netIdOf(int slot);                // The lobby slot of a player slot, or -1.
int slotOfNetId(int netId);           // The player slot of a lobby slot, or -1.
const Stats& stats();
// Ends co-op for this machine: forgets the run and every message buffer.
void stop();
// Swaps the link of the current session, for tests. Null puts the browser link back.
void setLink(Link* link);

// For tests: a second machine in the same program. Everything above works on the session in
// use, and the game only ever uses its own. Made once, outside any frame.
struct Session;
Session* createSession();
void destroySession(Session* session);
void useSession(Session* session);  // Null goes back to the game's own session.

// ---- Host ------------------------------------------------------------------------

// Starts a co-op run as host: steps sim::newRun with these settings, makes this machine slot 0,
// and sends each other player their copy of the settings (again and again until they answer).
// settings.netIds[0] must be the host's own lobby slot.
void hostStart(GameState& state, const RunStart& settings);

// Reads every message that arrived: inputs go into each player's queue, cards are kept for
// the next step. Call once per frame, before stepping.
void hostReceive();

// Fills one step of input for every other player, from their queue. inputs[0] (the host) is
// left alone. Call once before each sim::step. A player who left or is late is handled here.
void hostFillInputs(const GameState& state, InputFrame inputs[], int count);

// A player who left the room. They stay in the run, down and still, so the team can go on.
void hostSetConnected(int slot, bool connected);
bool hostConnected(int slot);

// A player of this run is connected again after being away (a reload, a dropped connection, or
// the same seat taken from another tab). Their soldier, level and build stayed here all along.
// Everything the host knew about their old connection is reset, and the run's settings are
// sent to them again until they answer, so a reloaded page builds the run from scratch. Flat
// cars are sent again from the first one. They are still down, and a teammate can revive them.
void hostPlayerBack(int slot);

// Tells a player in the room that the current run was made without them (they came back after
// the host started another run). Their game then leaves with "That run has ended".
void hostSendNotInRun(int netId);

// Before each step: keeps players who left down, so nobody can revive them.
void hostBeforeStep(GameState& state);

// After the frame's steps and before the events are cleared: keeps the events each player
// should see, and sends snapshots, flattened cars and the run's end when they are due.
// paused is true while the host's pause menu is open, away while the host's tab is hidden.
void hostAfterFrame(GameState& state, float dt, bool paused, bool away);

// Sends the pause state at once, without waiting for a frame. Used when the tab is hidden,
// because the browser may not run another frame until it comes back.
void hostSendPause(bool paused, bool away);

// The host starts another run in the same room. The settings are made from the room as it is
// now, so a player who left is not in it. The run number goes up by itself.
void hostRestart(GameState& state, const RunStart& settings);

// For the tests: zombies within the view radius of that player at the last snapshot.
int hostNearEnemies(int slot);

// ---- Client ----------------------------------------------------------------------

// Starts listening as a client: waits for the host's RunStart. Returns nothing; use clientNewRun.
void clientListen();

// True once per new run from the host: the RunStart has arrived and state was reset for it.
// The caller then shows the play screen.
bool clientNewRun(GameState& state);

// One drawn frame as a client: reads messages, sends input, predicts this player's own
// movement, and fills state with the world as it should be drawn now. moveX and moveY are the
// stick or keys, dash is a dash press this frame. While a menu is open, pass no movement.
void clientFrame(GameState& state, float dt, float moveX, float moveY, bool dash);

// Sends a level-up card or a reroll to the host, once for the cards on screen now.
void clientPickCard(const GameState& state, int card, bool reroll);

// How far the client is between two prediction steps, for drawing its own soldier smoothly.
float clientAlpha();

// What the client knows about the run's state that is not in GameState.
bool clientHostPaused();
bool clientHostAway();      // The host said so, or no snapshot came for a while.
bool clientRunEnded();      // The host sent the run's end.
bool clientNotInRun();      // The host said its current run was made without this player.
const RunEnd& clientRunEnd();

}  // namespace ls::netgame
