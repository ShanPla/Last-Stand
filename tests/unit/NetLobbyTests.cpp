#include "doctest.h"

#include "components/Net/Backend.h"
#include "components/Net/Net.h"
#include "core/Save.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace ls;

namespace {

// Every test starts from a sleeping fake server and an empty lobby, so tests never depend on
// each other. The lobby and the fake are global, like the real one will be.
void freshLobby() {
    net::setBackend(nullptr);
    net::debugCommand(net::kDebugReset, 0);
    net::leaveRoom();
}

// Runs time forward in small steps, as the game's frames would.
void settle(float seconds) {
    for (float t = 0.0f; t < seconds; t += 0.05f) net::update(0.05f);
}

bool nameIs(int slot, const char* name) { return std::strcmp(net::lobby().players[slot].name, name) == 0; }

}  // namespace

TEST_CASE("names: 1 to 12 letters, digits and spaces, trimmed") {
    CHECK(net::validName("Ana"));
    CHECK(net::validName("A"));
    CHECK(net::validName("Player 2"));
    CHECK(net::validName("abcdefghij12"));      // Exactly 12.
    CHECK_FALSE(net::validName("abcdefghij123"));  // 13.
    CHECK_FALSE(net::validName(""));
    CHECK_FALSE(net::validName(" Ana"));
    CHECK_FALSE(net::validName("Ana "));
    CHECK_FALSE(net::validName("   "));
    CHECK_FALSE(net::validName("Ana!"));
    CHECK_FALSE(net::validName("Ana_B"));
    CHECK_FALSE(net::validName("Zo\xC3\xAB"));   // Not plain ASCII.
    CHECK_FALSE(net::validName(nullptr));
}

TEST_CASE("codes: 5 characters from the alphabet, in either case") {
    CHECK(net::validCode("K7QXM"));
    CHECK(net::validCode("k7qxm"));
    CHECK(net::validCode("ABCDE"));
    CHECK(net::validCode("23456"));
    CHECK_FALSE(net::validCode("K7QX"));       // Too short.
    CHECK_FALSE(net::validCode("K7QXMA"));     // Too long.
    CHECK_FALSE(net::validCode(""));
    CHECK_FALSE(net::validCode(nullptr));
    // The five look-alike characters are not in the alphabet.
    for (const char* bad : {"0AAAA", "OAAAA", "1AAAA", "IAAAA", "LAAAA", "AAAA0", "AAAAl"}) CHECK_FALSE(net::validCode(bad));
    CHECK_FALSE(net::validCode("K7 XM"));
    CHECK_FALSE(net::validCode("K7-XM"));
    // Every alphabet character is accepted, so the alphabet and the check agree.
    for (const char* c = net::kCodeAlphabet; *c; ++c) {
        const char code[6] = {*c, *c, *c, *c, *c, '\0'};
        CHECK(net::validCode(code));
    }
    CHECK(std::strlen(net::kCodeAlphabet) == 30);
    CHECK_FALSE(net::validCode("ZZZZZ"));  // Z is left out, because the pixel font draws it like 2.
}

TEST_CASE("single characters: what the text field lets through") {
    CHECK(net::nameCharAllowed('a'));
    CHECK(net::nameCharAllowed('Z'));
    CHECK(net::nameCharAllowed('7'));
    CHECK(net::nameCharAllowed(' '));
    CHECK_FALSE(net::nameCharAllowed('-'));
    CHECK_FALSE(net::nameCharAllowed('\0'));
    CHECK(net::codeCharAllowed('k'));
    CHECK(net::codeCharAllowed('K'));
    CHECK(net::codeCharAllowed('9'));
    CHECK_FALSE(net::codeCharAllowed('0'));
    CHECK_FALSE(net::codeCharAllowed('O'));
    CHECK_FALSE(net::codeCharAllowed('l'));
    CHECK_FALSE(net::codeCharAllowed(' '));
    CHECK_FALSE(net::codeCharAllowed('\0'));
}

TEST_CASE("a new lobby is offline and empty") {
    freshLobby();
    const net::Lobby& lobby = net::lobby();
    CHECK(lobby.phase == net::LobbyPhase::Offline);
    CHECK(lobby.error == net::LobbyError::None);
    CHECK(lobby.code[0] == '\0');
    CHECK(lobby.count == 0);
    CHECK(lobby.you == -1);
    CHECK(lobby.host == -1);
    CHECK_FALSE(net::canStart());
}

TEST_CASE("waking the server shows Waking, then goes back to Offline") {
    freshLobby();
    net::wakeServer();
    CHECK(net::lobby().phase == net::LobbyPhase::Waking);
    settle(0.2f);
    CHECK(net::lobby().phase == net::LobbyPhase::Waking);
    CHECK(net::lobby().wakeSeconds > 0.1f);
    settle(0.6f);
    CHECK(net::lobby().phase == net::LobbyPhase::Offline);
    // Waking an awake server changes nothing.
    net::wakeServer();
    CHECK(net::lobby().phase == net::LobbyPhase::Offline);
}

TEST_CASE("a server that never wakes keeps Waking, and its timer reaches the retry time") {
    freshLobby();
    net::debugCommand(net::kDebugWakeDelayMs, -1);
    net::wakeServer();
    settle(net::kWakeRetrySeconds + 1.0f);
    CHECK(net::lobby().phase == net::LobbyPhase::Waking);
    CHECK(net::lobby().wakeSeconds >= net::kWakeRetrySeconds);
    // Retry starts the wait again from zero.
    net::retry();
    CHECK(net::lobby().phase == net::LobbyPhase::Waking);
    CHECK(net::lobby().wakeSeconds < 1.0f);
}

TEST_CASE("hosting a room goes Waking, Connecting, InLobby with you as the host") {
    freshLobby();
    net::hostRoom("Ana");
    CHECK(net::lobby().phase == net::LobbyPhase::Waking);  // The fake server starts asleep.
    settle(0.55f);
    CHECK(net::lobby().phase == net::LobbyPhase::Connecting);
    settle(0.4f);
    const net::Lobby& lobby = net::lobby();
    REQUIRE(lobby.phase == net::LobbyPhase::InLobby);
    CHECK(net::validCode(lobby.code));
    CHECK(lobby.you == 0);
    CHECK(lobby.host == 0);
    CHECK(lobby.count == 1);
    CHECK(lobby.players[0].id == 0);
    CHECK(nameIs(0, "Ana"));
    CHECK_FALSE(lobby.players[0].ready);
    CHECK_FALSE(lobby.started);
}

TEST_CASE("an awake server skips Waking") {
    freshLobby();
    net::wakeServer();
    settle(0.7f);
    net::hostRoom("Ana");
    CHECK(net::lobby().phase == net::LobbyPhase::Connecting);
    settle(0.4f);
    CHECK(net::lobby().phase == net::LobbyPhase::InLobby);
}

TEST_CASE("a bad name fails at once, without waiting for the server") {
    freshLobby();
    net::hostRoom("");
    CHECK(net::lobby().phase == net::LobbyPhase::Error);
    CHECK(net::lobby().error == net::LobbyError::BadName);
    net::hostRoom("bad!name");
    CHECK(net::lobby().error == net::LobbyError::BadName);
    net::hostRoom("      ");
    CHECK(net::lobby().error == net::LobbyError::BadName);
    net::joinRoom("K7QXM", "x y!");
    CHECK(net::lobby().error == net::LobbyError::BadName);
    net::hostRoom("thirteen chars");
    CHECK(net::lobby().error == net::LobbyError::BadName);
}

TEST_CASE("names are trimmed before they are sent") {
    freshLobby();
    net::hostRoom("  Ana  ");
    settle(1.2f);
    REQUIRE(net::lobby().phase == net::LobbyPhase::InLobby);
    CHECK(nameIs(0, "Ana"));
}

TEST_CASE("joining: the code is upper-cased, and you take slot 1 behind the host") {
    freshLobby();
    net::joinRoom("k7qxm", "Ben");
    settle(1.2f);
    const net::Lobby& lobby = net::lobby();
    REQUIRE(lobby.phase == net::LobbyPhase::InLobby);
    CHECK(std::string(lobby.code) == "K7QXM");
    CHECK(lobby.you == 1);
    CHECK(lobby.host == 0);
    CHECK(lobby.count == 2);
    CHECK(nameIs(1, "Ben"));
    CHECK_FALSE(net::canStart());  // Only the host can start.
}

TEST_CASE("a join with something that cannot be a code is NotFound, without waiting") {
    freshLobby();
    net::joinRoom("K7Q", "Ben");
    CHECK(net::lobby().phase == net::LobbyPhase::Error);
    CHECK(net::lobby().error == net::LobbyError::NotFound);
    net::joinRoom("0OIL1", "Ben");
    CHECK(net::lobby().error == net::LobbyError::NotFound);
    net::joinRoom(nullptr, "Ben");
    CHECK(net::lobby().error == net::LobbyError::NotFound);
}

TEST_CASE("every error the fake can give is shown, for both host and join") {
    const net::LobbyError errors[] = {net::LobbyError::ServerUnreachable, net::LobbyError::NotFound, net::LobbyError::Full,
                                      net::LobbyError::Started,           net::LobbyError::BadName,  net::LobbyError::Version,
                                      net::LobbyError::Other};
    for (const net::LobbyError error : errors) {
        freshLobby();
        net::debugCommand(net::kDebugNextError, static_cast<int>(error));
        net::joinRoom("K7QXM", "Ben");
        settle(1.2f);
        CHECK(net::lobby().phase == net::LobbyPhase::Error);
        CHECK(net::lobby().error == error);
        CHECK(net::lobby().code[0] == '\0');

        freshLobby();
        net::debugCommand(net::kDebugNextError, static_cast<int>(error));
        net::hostRoom("Ana");
        settle(1.2f);
        CHECK(net::lobby().phase == net::LobbyPhase::Error);
        CHECK(net::lobby().error == error);
    }
}

TEST_CASE("an error happens once: Retry then succeeds, and Leave clears the error") {
    freshLobby();
    net::debugCommand(net::kDebugNextError, static_cast<int>(net::LobbyError::ServerUnreachable));
    net::joinRoom("K7QXM", "Ben");
    settle(1.2f);
    REQUIRE(net::lobby().phase == net::LobbyPhase::Error);
    net::retry();  // Repeats the same join.
    settle(0.9f);
    REQUIRE(net::lobby().phase == net::LobbyPhase::InLobby);
    CHECK(std::string(net::lobby().code) == "K7QXM");
    CHECK(nameIs(1, "Ben"));

    freshLobby();
    net::debugCommand(net::kDebugNextError, static_cast<int>(net::LobbyError::Full));
    net::hostRoom("Ana");
    settle(1.2f);
    REQUIRE(net::lobby().phase == net::LobbyPhase::Error);
    net::leaveRoom();
    CHECK(net::lobby().phase == net::LobbyPhase::Offline);
    CHECK(net::lobby().error == net::LobbyError::None);
}

TEST_CASE("Ready toggles for you only, and does nothing outside a room") {
    freshLobby();
    net::setReady(true);
    CHECK(net::lobby().phase == net::LobbyPhase::Offline);
    net::debugCommand(net::kDebugRoom, 3);
    REQUIRE(net::lobby().count == 3);
    net::setReady(true);
    CHECK(net::lobby().players[0].ready);
    CHECK_FALSE(net::lobby().players[1].ready);
    CHECK_FALSE(net::lobby().players[2].ready);
    net::setReady(false);
    CHECK_FALSE(net::lobby().players[0].ready);
}

TEST_CASE("Start needs the host, two players, and everyone ready") {
    freshLobby();
    net::debugCommand(net::kDebugRoom, 1);
    net::setReady(true);
    CHECK_FALSE(net::canStart());  // Alone.

    net::debugCommand(net::kDebugRoom, 3);
    CHECK_FALSE(net::canStart());  // Nobody ready.
    net::debugCommand(net::kDebugReadyMask, 0b011);
    CHECK_FALSE(net::canStart());  // One of three is not.
    net::debugCommand(net::kDebugReadyMask, 0b111);
    CHECK(net::canStart());
    net::debugCommand(net::kDebugReadyMask, 0b101);
    CHECK_FALSE(net::canStart());  // You are not ready.
    net::debugCommand(net::kDebugReadyMask, 0b111);

    // A full room works the same.
    net::debugCommand(net::kDebugRoom, 4);
    net::debugCommand(net::kDebugReadyMask, 0b1111);
    CHECK(net::canStart());

    // A guest never can, even when everyone is ready.
    net::debugCommand(net::kDebugGuestRoom, 3);
    net::debugCommand(net::kDebugReadyMask, 0b111);
    CHECK(net::lobby().you == 1);
    CHECK_FALSE(net::canStart());
}

TEST_CASE("Start counts only the players who are here: an away player neither makes the two nor holds it up") {
    freshLobby();
    net::debugCommand(net::kDebugRoom, 3);
    net::debugCommand(net::kDebugReadyMask, 0b111);
    CHECK(net::canStart());
    // One of three is away, and not ready: the two who are here can still start.
    net::debugCommand(net::kDebugReadyMask, 0b011);
    net::debugCommand(net::kDebugAwayMask, 0b100);
    CHECK(net::hereCount() == 2);
    CHECK(net::canStart());
    // But two present players who are not both ready can't.
    net::debugCommand(net::kDebugReadyMask, 0b001);
    CHECK_FALSE(net::canStart());
    // Everyone else away leaves the host alone, so no co-op run can start, however ready they were.
    net::debugCommand(net::kDebugReadyMask, 0b111);
    net::debugCommand(net::kDebugAwayMask, 0b110);
    CHECK(net::hereCount() == 1);
    CHECK_FALSE(net::canStart());
    net::startRun();
    CHECK(net::lobby().phase == net::LobbyPhase::InLobby);
    CHECK_FALSE(net::lobby().started);
}

TEST_CASE("Start with the conditions not met does nothing, and with them met moves to Starting") {
    freshLobby();
    net::debugCommand(net::kDebugRoom, 2);
    net::startRun();
    CHECK(net::lobby().phase == net::LobbyPhase::InLobby);
    CHECK_FALSE(net::lobby().started);
    net::debugCommand(net::kDebugReadyMask, 0b11);
    net::startRun();
    CHECK(net::lobby().phase == net::LobbyPhase::Starting);
    CHECK(net::lobby().started);
    CHECK_FALSE(net::canStart());  // Once is enough.
    net::setReady(false);          // And a started room no longer takes ready changes.
    CHECK(net::lobby().players[0].ready);
}

TEST_CASE("Leave empties the lobby") {
    freshLobby();
    net::debugCommand(net::kDebugRoom, 4);
    REQUIRE(net::lobby().count == 4);
    net::leaveRoom();
    const net::Lobby& lobby = net::lobby();
    CHECK(lobby.phase == net::LobbyPhase::Offline);
    CHECK(lobby.count == 0);
    CHECK(lobby.code[0] == '\0');
    CHECK(lobby.you == -1);
    CHECK(lobby.host == -1);
}

TEST_CASE("the host leaving and the server dropping are errors the screens can recognize") {
    freshLobby();
    net::debugCommand(net::kDebugGuestRoom, 3);
    net::debugCommand(net::kDebugHostLeft, 0);
    CHECK(net::lobby().phase == net::LobbyPhase::Error);
    CHECK(net::lobby().error == net::LobbyError::HostLeft);

    freshLobby();
    net::debugCommand(net::kDebugRoom, 2);
    net::debugCommand(net::kDebugLoseServer, 0);
    CHECK(net::lobby().phase == net::LobbyPhase::Error);
    CHECK(net::lobby().error == net::LobbyError::ServerUnreachable);
}

TEST_CASE("debug rooms: player count, you, and the host slot") {
    freshLobby();
    for (int players = 1; players <= 4; ++players) {
        net::debugCommand(net::kDebugRoom, players);
        CHECK(net::lobby().phase == net::LobbyPhase::InLobby);
        CHECK(net::lobby().count == players);
        CHECK(net::lobby().you == 0);
        CHECK(net::lobby().host == 0);
        for (int i = 0; i < players; ++i) CHECK(net::lobby().players[i].id == i);
    }
    net::debugCommand(net::kDebugRoom, 9);  // Too many is capped at 4.
    CHECK(net::lobby().count == 4);
    net::debugCommand(net::kDebugGuestRoom, 1);  // A guest needs a host, so at least two.
    CHECK(net::lobby().count == 2);
    CHECK(net::lobby().you == 1);
}

namespace {

// A backend that only records what it was asked, to prove the seam: Net checks first, and the
// backend only sees clean requests.
class RecordingBackend final : public net::Backend {
public:
    void wake(net::Lobby&) override { wakes++; }
    void host(net::Lobby&, const char* name) override { hostName = name; hosts++; }
    void join(net::Lobby&, const char* code, const char* name) override { joinCode = code; joinName = name; joins++; }
    void setReady(net::Lobby&, bool) override {}
    void start(net::Lobby&) override { starts++; }
    void leave(net::Lobby&) override { leaves++; }
    void update(net::Lobby&, float) override { updates++; }
    void setSettings(net::Lobby&, int map, int difficulty, int mode) override {
        settingsSent++;
        lastMap = map;
        lastDifficulty = difficulty;
        lastMode = mode;
    }
    // Puts the lobby in a room by hand: kDebugRoom as the host, kDebugGuestRoom as the second player.
    void debug(net::Lobby& lobby, int op, int) override {
        if (op != net::kDebugRoom && op != net::kDebugGuestRoom) return;
        lobby = net::Lobby{};
        lobby.phase = net::LobbyPhase::InLobby;
        std::snprintf(lobby.code, sizeof(lobby.code), "%s", roomCode);
        lobby.count = 2;
        lobby.host = 0;
        lobby.you = op == net::kDebugRoom ? 0 : 1;
        lobby.players[0].id = 0;
        lobby.players[1].id = 1;
    }
    void rejoin(net::Lobby&) override { rejoins++; }
    const char* rejoinCode() const override { return seat ? "K7QXM" : ""; }
    void forgetRejoin() override { seat = false; }
    void abandon(net::Lobby& lobby, net::LobbyError error) override {
        seat = false;
        lobby.phase = net::LobbyPhase::Error;
        lobby.error = error;
    }
    int wakes = 0, hosts = 0, joins = 0, starts = 0, leaves = 0, updates = 0, rejoins = 0;
    int settingsSent = 0, lastMap = -1, lastDifficulty = -1, lastMode = -1;
    const char* roomCode = "K7QXM";
    bool seat = false;
    std::string hostName, joinCode, joinName;
};

}  // namespace

TEST_CASE("the backend seam: a replacement backend only receives checked requests") {
    freshLobby();
    RecordingBackend recorder;
    net::setBackend(&recorder);
    net::wakeServer();
    net::hostRoom("bad!");           // Refused before it reaches the backend.
    net::joinRoom("short", "Ana");   // Not a code.
    net::joinRoom("K7QXM", "");      // Not a name.
    CHECK(recorder.hosts == 0);
    CHECK(recorder.joins == 0);
    net::hostRoom("  Ana ");
    net::joinRoom("k7qxm", "Ben");
    CHECK(recorder.hosts == 1);
    CHECK(recorder.hostName == "Ana");
    CHECK(recorder.joins == 1);
    CHECK(recorder.joinCode == "K7QXM");
    CHECK(recorder.joinName == "Ben");
    net::startRun();  // Not in a room, so it is not sent.
    CHECK(recorder.starts == 0);
    net::update(0.016f);
    CHECK(recorder.updates == 1);
    net::leaveRoom();
    CHECK(recorder.leaves == 1);
    // Rejoin only reaches the backend when there is a seat to take back.
    net::rejoinRoom();
    CHECK(recorder.rejoins == 0);
    recorder.seat = true;
    CHECK(net::canRejoin());
    net::rejoinRoom();
    CHECK(recorder.rejoins == 1);
    net::setBackend(nullptr);
    freshLobby();
}

TEST_CASE("room settings: the host sends them once, and again only when they change") {
    freshLobby();
    RecordingBackend recorder;
    net::setBackend(&recorder);
    net::setRoomSettings(1, 2, 0);          // Not in a room yet: nothing is sent.
    net::update(0.016f);
    CHECK(recorder.settingsSent == 0);

    net::debugCommand(net::kDebugRoom, 2);   // The host of a room.
    net::update(0.016f);                     // The room is up, so what was wanted goes out at once.
    CHECK(recorder.settingsSent == 1);
    CHECK(recorder.lastMap == 1);
    CHECK(recorder.lastDifficulty == 2);
    CHECK(recorder.lastMode == 0);

    // Called every frame, as the screen does: still one send.
    for (int i = 0; i < 100; ++i) {
        net::setRoomSettings(1, 2, 0);
        net::update(0.016f);
    }
    CHECK(recorder.settingsSent == 1);

    net::setRoomSettings(1, 3, 0);           // One value changes: one more send, with all three.
    CHECK(recorder.settingsSent == 2);
    CHECK(recorder.lastDifficulty == 3);
    net::setRoomSettings(0, 3, 1);
    CHECK(recorder.settingsSent == 3);
    CHECK(recorder.lastMap == 0);
    CHECK(recorder.lastMode == 1);

    // Another room (a new code) hears them again, even when nothing changed.
    net::leaveRoom();
    recorder.roomCode = "AB234";
    net::debugCommand(net::kDebugRoom, 2);
    net::update(0.016f);
    CHECK(recorder.settingsSent == 4);
    net::update(0.016f);
    CHECK(recorder.settingsSent == 4);
    net::setBackend(nullptr);
    freshLobby();
}

TEST_CASE("room settings: only the host sends, and bad values are not sent") {
    freshLobby();
    RecordingBackend recorder;
    net::setBackend(&recorder);

    // A guest (slot 1 of a room whose host is slot 0) sends nothing.
    net::debugCommand(net::kDebugGuestRoom, 2);
    net::setRoomSettings(1, 1, 1);
    net::update(0.016f);
    CHECK(recorder.settingsSent == 0);

    // Out of range numbers never leave this machine. The previous good choice stays wanted.
    net::debugCommand(net::kDebugRoom, 2);
    net::update(0.016f);                     // The room is ours now, so the last good choice (1, 1, 1) goes out.
    net::setRoomSettings(-1, 1, 1);
    net::setRoomSettings(1, net::kSettingMax + 1, 1);
    net::setRoomSettings(1, 1, -5);
    CHECK(recorder.settingsSent == 1);       // Just that one: the three bad calls sent nothing.
    CHECK(recorder.lastMap == 1);
    CHECK(recorder.lastDifficulty == 1);
    CHECK(recorder.lastMode == 1);
    net::setRoomSettings(net::kSettingMax, 0, 0);   // The largest allowed value is fine.
    CHECK(recorder.settingsSent == 2);
    CHECK(recorder.lastMap == net::kSettingMax);

    // Outside a room nothing happens, and nothing is left over for the next one.
    net::leaveRoom();
    net::update(0.016f);
    net::setRoomSettings(0, 0, 0);
    CHECK(recorder.settingsSent == 2);
    net::setBackend(nullptr);
    freshLobby();
}

TEST_CASE("room settings on the fake: the host's choice is what the lobby shows, a guest's room starts empty") {
    freshLobby();
    net::debugCommand(net::kDebugRoom, 2);
    CHECK_FALSE(net::lobby().settings.known);
    CHECK(net::lobby().settings.map == -1);
    net::setRoomSettings(1, 3, 1);
    CHECK(net::lobby().settings.known);
    CHECK(net::lobby().settings.map == 1);
    CHECK(net::lobby().settings.difficulty == 3);
    CHECK(net::lobby().settings.mode == 1);
    net::setRoomSettings(0, 3, 1);
    CHECK(net::lobby().settings.map == 0);

    // A guest never sets them. They only change when the (fake) host says so.
    net::debugCommand(net::kDebugGuestRoom, 3);
    net::setRoomSettings(1, 1, 1);
    net::update(0.016f);
    CHECK_FALSE(net::lobby().settings.known);
    net::debugCommand(net::kDebugRoomSettings, 1 + 16 * 2 + 256 * 1);
    CHECK(net::lobby().settings.known);
    CHECK(net::lobby().settings.map == 1);
    CHECK(net::lobby().settings.difficulty == 2);
    CHECK(net::lobby().settings.mode == 1);
    net::debugCommand(net::kDebugRoomSettings, -1);
    CHECK_FALSE(net::lobby().settings.known);
    net::leaveRoom();
    CHECK_FALSE(net::lobby().settings.known);
    freshLobby();
}

TEST_CASE("rejoin on the fake: the button's seat, back into a started run, and refusals forget the seat") {
    freshLobby();
    net::debugCommand(net::kDebugWakeDelayMs, 0);
    net::debugCommand(net::kDebugLatencyMs, 0);
    CHECK_FALSE(net::canRejoin());
    CHECK(std::string(net::rejoinCode()).empty());
    net::rejoinRoom();  // No seat: nothing happens.
    CHECK(net::lobby().phase == net::LobbyPhase::Offline);

    net::debugCommand(net::kDebugRejoinSeat, 1);
    CHECK(net::canRejoin());
    CHECK(std::string(net::rejoinCode()) == "K7QXM");
    net::rejoinRoom();
    settle(0.3f);
    CHECK(net::lobby().phase == net::LobbyPhase::Starting);
    CHECK(net::lobby().started);
    CHECK(net::lobby().you == 1);
    CHECK(std::string(net::lobby().code) == "K7QXM");
    CHECK(net::hereCount() == 2);

    // Away players stay listed, but do not count as here.
    net::debugCommand(net::kDebugAwayMask, 2);
    CHECK(net::lobby().count == 2);
    CHECK(net::lobby().players[1].away);
    CHECK_FALSE(net::isHere(net::lobby().players[1]));
    CHECK(net::hereCount() == 1);

    // Leaving keeps the seat (a closed tab), leaving for good forgets it.
    net::leaveRoom();
    CHECK(net::canRejoin());
    net::forgetRejoin();
    CHECK_FALSE(net::canRejoin());

    // A refused rejoin shows its error and forgets the seat, so the button goes away.
    for (const net::LobbyError error : {net::LobbyError::RunEnded, net::LobbyError::RoomClosed}) {
        net::debugCommand(net::kDebugRejoinSeat, 1);
        net::debugCommand(net::kDebugNextError, static_cast<int>(error));
        net::rejoinRoom();
        settle(0.3f);
        CHECK(net::lobby().phase == net::LobbyPhase::Error);
        CHECK(net::lobby().error == error);
        CHECK_FALSE(net::canRejoin());
        net::leaveRoom();
    }
    // A connection problem is worth a retry, and keeps the seat.
    net::debugCommand(net::kDebugRejoinSeat, 1);
    net::debugCommand(net::kDebugNextError, static_cast<int>(net::LobbyError::ServerUnreachable));
    net::rejoinRoom();
    settle(0.3f);
    CHECK(net::lobby().error == net::LobbyError::ServerUnreachable);
    CHECK(net::canRejoin());
    net::retry();
    settle(0.3f);
    CHECK(net::lobby().phase == net::LobbyPhase::Starting);

    // The game learns the run is over for this player: Error, RunEnded, and no seat.
    net::abandonRun(net::LobbyError::RunEnded);
    CHECK(net::lobby().phase == net::LobbyPhase::Error);
    CHECK(net::lobby().error == net::LobbyError::RunEnded);
    CHECK_FALSE(net::canRejoin());
    freshLobby();
}

TEST_CASE("the saved co-op name: round trip, old saves, and damaged values") {
    SaveData data;
    CHECK(data.coopName[0] == '\0');
    std::snprintf(data.coopName, sizeof(data.coopName), "%s", "Ana Maria");
    const SaveData back = save::parse(save::serialize(data));
    CHECK(std::string(back.coopName) == "Ana Maria");

    // A save with no such key (written before co-op existed) loads with no name.
    const SaveData old = save::parse("version=1\nmusic=0.500\nmap=1\n");
    CHECK(old.coopName[0] == '\0');
    CHECK(old.map == 1);

    // A name the server would refuse is dropped, and the rest of the file still loads.
    const SaveData bad = save::parse("coopName=Ana!!\nmusic=0.250\n");
    CHECK(bad.coopName[0] == '\0');
    CHECK(bad.settings.musicVolume == doctest::Approx(0.25f));
    const SaveData tooLong = save::parse("coopName=abcdefghijklmnopqrstuvwxyz\n");
    CHECK(tooLong.coopName[0] == '\0');
    const SaveData empty = save::parse("coopName=\n");
    CHECK(empty.coopName[0] == '\0');
    // Spaces around the value are trimmed by the parser.
    const SaveData spaced = save::parse("coopName=   Ben  \n");
    CHECK(std::string(spaced.coopName) == "Ben");
}
