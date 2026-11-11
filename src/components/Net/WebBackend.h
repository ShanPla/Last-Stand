#pragma once

#include "components/Net/Net.h"

// The reading half of the web backend, in a header only so unit tests can feed it text. The
// browser script (web/components/NetClient) describes the lobby as JSON, and these turn that
// JSON into the Lobby the screens draw. Both are plain C++ with no browser code.
namespace ls::net {

// Reads the script's snapshot (snapshotJson) into the lobby. Everything is replaced except
// wakeSeconds, which the backend counts itself. Returns false, and leaves the lobby alone,
// when the text is not a snapshot.
bool applyWebSnapshot(const char* json, Lobby& lobby);

// Adds the per-second message counts from the script's statsJson to the players of the lobby.
void applyWebStats(const char* json, Lobby& lobby);

}  // namespace ls::net
