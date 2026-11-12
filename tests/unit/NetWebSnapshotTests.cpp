#include "doctest.h"

#include "components/Net/Net.h"
#include "components/Net/WebBackend.h"

#include <cstdio>
#include <cstring>

using namespace ls;

// These feed the web backend's reader the same text the browser script produces (see snapshot()
// and stats() in web/components/NetClient/NetClient.js), so the C++ and JavaScript halves cannot
// drift apart without a test saying so.

namespace {

const char kGuestInRoom[] =
    "{\"phase\":\"InLobby\",\"phaseIndex\":3,\"wake\":{\"state\":\"awake\",\"ms\":812,\"attempts\":1},"
    "\"error\":\"\",\"errorIndex\":-1,\"errorCode\":\"\",\"code\":\"K7QXM\",\"myId\":2,\"hostId\":0,\"isHost\":false,"
    "\"started\":false,\"players\":["
    "{\"id\":0,\"name\":\"Ana\",\"ready\":true,\"isHost\":true,\"isMe\":false,\"transport\":\"webrtc\",\"rttMs\":31},"
    "{\"id\":2,\"name\":\"Ben Two\",\"ready\":false,\"isHost\":false,\"isMe\":true,\"transport\":\"local\",\"rttMs\":-1}],"
    "\"serverRttMs\":44,\"forceRelay\":false}";

const char kStats[] =
    "{\"serverRttMs\":44,\"dropped\":0,\"peers\":{\"0\":{\"transport\":\"webrtc\",\"rttMs\":31,\"msgsInPerSec\":58,"
    "\"msgsOutPerSec\":30,\"bytesInPerSec\":9000,\"bytesOutPerSec\":400}},"
    "\"total\":{\"msgsIn\":58,\"msgsOut\":30,\"bytesIn\":9000,\"bytesOut\":400}}";

net::Lobby parse(const char* json) {
    net::Lobby lobby;
    CHECK(net::applyWebSnapshot(json, lobby));
    return lobby;
}

}  // namespace

TEST_CASE("web snapshot: a joined room, with a gap in the server's slots") {
    net::Lobby lobby = parse(kGuestInRoom);
    CHECK(lobby.phase == net::LobbyPhase::InLobby);
    CHECK(lobby.error == net::LobbyError::None);
    CHECK(std::strcmp(lobby.code, "K7QXM") == 0);
    CHECK(lobby.count == 2);
    // The screens use positions as rows, so slot 2 sits in row 1 and keeps its server id.
    CHECK(lobby.you == 1);
    CHECK(lobby.host == 0);
    CHECK(lobby.players[1].id == 2);
    CHECK(std::strcmp(lobby.players[0].name, "Ana") == 0);
    CHECK(std::strcmp(lobby.players[1].name, "Ben Two") == 0);
    CHECK(lobby.players[0].ready);
    CHECK_FALSE(lobby.players[1].ready);
    CHECK(lobby.players[0].transport == net::Transport::WebRtc);
    CHECK(lobby.players[1].transport == net::Transport::Local);
    CHECK(lobby.players[0].rttMs == 31);
    CHECK(lobby.serverRttMs == 44);
    CHECK_FALSE(lobby.started);

    net::applyWebStats(kStats, lobby);
    CHECK(lobby.players[0].msgsInPerSec == 58);
    CHECK(lobby.players[0].msgsOutPerSec == 30);
    CHECK(lobby.players[1].msgsInPerSec == 0);  // This player has no pair with itself.
}

TEST_CASE("web snapshot: phases and errors use the names both sides agreed on") {
    const char* phases[] = {"Offline", "Waking", "Connecting", "InLobby", "Starting", "Error"};
    const net::LobbyPhase expected[] = {net::LobbyPhase::Offline,  net::LobbyPhase::Waking,   net::LobbyPhase::Connecting,
                                        net::LobbyPhase::InLobby,  net::LobbyPhase::Starting, net::LobbyPhase::Error};
    for (int i = 0; i < 6; ++i) {
        char json[512];
        std::snprintf(json, sizeof(json),
                      "{\"phase\":\"%s\",\"phaseIndex\":%d,\"wake\":{\"state\":\"idle\",\"ms\":0,\"attempts\":0},\"error\":\"\",\"errorIndex\":%d,"
                      "\"errorCode\":\"\",\"code\":\"\",\"myId\":-1,\"hostId\":-1,\"isHost\":false,\"started\":false,\"players\":[],"
                      "\"serverRttMs\":-1,\"forceRelay\":false}",
                      phases[i], i, i == 5 ? 5 : -1);
        net::Lobby lobby = parse(json);
        CHECK(lobby.phase == expected[i]);
        CHECK(lobby.count == 0);
        CHECK(lobby.you == -1);
        CHECK(lobby.host == -1);
    }

    // The script's error list: ServerUnreachable, NotFound, Full, Started, BadName, HostLeft, Version, Other.
    const net::LobbyError errors[] = {net::LobbyError::ServerUnreachable, net::LobbyError::NotFound, net::LobbyError::Full,    net::LobbyError::Started,
                                      net::LobbyError::BadName,           net::LobbyError::HostLeft,  net::LobbyError::Version, net::LobbyError::Other};
    for (int i = 0; i < 8; ++i) {
        char json[512];
        std::snprintf(json, sizeof(json),
                      "{\"phase\":\"Error\",\"phaseIndex\":5,\"wake\":{\"state\":\"awake\",\"ms\":0,\"attempts\":1},\"error\":\"x\",\"errorIndex\":%d,"
                      "\"errorCode\":\"y\",\"code\":\"\",\"myId\":-1,\"hostId\":-1,\"isHost\":false,\"started\":false,\"players\":[],"
                      "\"serverRttMs\":-1,\"forceRelay\":false}",
                      i);
        CHECK(parse(json).error == errors[i]);
    }
}

TEST_CASE("web snapshot: connecting while the server still wakes is shown as waking, and the timer survives") {
    net::Lobby lobby;
    lobby.wakeSeconds = 12.5f;
    const char json[] =
        "{\"phase\":\"Connecting\",\"phaseIndex\":2,\"wake\":{\"state\":\"waking\",\"ms\":4000,\"attempts\":3},\"error\":\"\",\"errorIndex\":-1,"
        "\"errorCode\":\"\",\"code\":\"\",\"myId\":-1,\"hostId\":-1,\"isHost\":false,\"started\":false,\"players\":[],"
        "\"serverRttMs\":-1,\"forceRelay\":false}";
    CHECK(net::applyWebSnapshot(json, lobby));
    CHECK(lobby.phase == net::LobbyPhase::Waking);
    CHECK(lobby.wakeSeconds == 12.5f);
}

TEST_CASE("web snapshot: a started room, and a host reading itself") {
    const char json[] =
        "{\"phase\":\"Starting\",\"phaseIndex\":4,\"wake\":{\"state\":\"awake\",\"ms\":0,\"attempts\":1},\"error\":\"\",\"errorIndex\":-1,"
        "\"errorCode\":\"\",\"code\":\"AB234\",\"myId\":0,\"hostId\":0,\"isHost\":true,\"started\":true,\"players\":["
        "{\"id\":0,\"name\":\"Host\",\"ready\":true,\"isHost\":true,\"isMe\":true,\"transport\":\"local\",\"rttMs\":-1},"
        "{\"id\":1,\"name\":\"Guest\",\"ready\":true,\"isHost\":false,\"isMe\":false,\"transport\":\"relay\",\"rttMs\":90}],"
        "\"serverRttMs\":-1,\"forceRelay\":true}";
    net::Lobby lobby = parse(json);
    CHECK(lobby.phase == net::LobbyPhase::Starting);
    CHECK(lobby.started);
    CHECK(lobby.you == 0);
    CHECK(lobby.host == 0);
    CHECK(lobby.players[1].transport == net::Transport::Relay);
    CHECK(lobby.players[1].rttMs == 90);
    CHECK(std::strcmp(net::transportName(lobby.players[1].transport), "relay") == 0);
}

TEST_CASE("web snapshot: damaged text is refused and bad names are made safe") {
    net::Lobby lobby;
    lobby.count = 3;
    CHECK_FALSE(net::applyWebSnapshot("", lobby));
    CHECK_FALSE(net::applyWebSnapshot("not json", lobby));
    CHECK_FALSE(net::applyWebSnapshot("{\"phaseIndex\":3,\"players\":[{\"id\":0", lobby));
    CHECK_FALSE(net::applyWebSnapshot(nullptr, lobby));
    CHECK(lobby.count == 3);  // Untouched by every refusal.

    // Five players cannot fit, an id outside 0 to 3 is skipped, and odd characters in a name are replaced.
    const char json[] =
        "{\"phase\":\"InLobby\",\"phaseIndex\":3,\"wake\":{\"state\":\"awake\",\"ms\":0,\"attempts\":1},\"error\":\"\",\"errorIndex\":-1,"
        "\"errorCode\":\"\",\"code\":\"ABCDEFGHIJ\",\"myId\":0,\"hostId\":0,\"isHost\":true,\"started\":false,\"players\":["
        "{\"id\":0,\"name\":\"A-}b\",\"ready\":false,\"transport\":\"local\",\"rttMs\":-1},"
        "{\"id\":9,\"name\":\"Out\",\"ready\":false,\"transport\":\"none\",\"rttMs\":-1},"
        "{\"id\":1,\"name\":\"abcdefghijklmnopqrstuvwxyz\",\"ready\":false,\"transport\":\"none\",\"rttMs\":-1},"
        "{\"id\":2,\"name\":\"C\",\"ready\":false,\"transport\":\"none\",\"rttMs\":-1},"
        "{\"id\":3,\"name\":\"D\",\"ready\":false,\"transport\":\"none\",\"rttMs\":-1},"
        "{\"id\":3,\"name\":\"E\",\"ready\":false,\"transport\":\"none\",\"rttMs\":-1}],"
        "\"serverRttMs\":-1,\"forceRelay\":false}";
    net::Lobby parsed = parse(json);
    CHECK(parsed.count == net::kMaxPlayers);
    CHECK(std::strcmp(parsed.players[0].name, "A??b") == 0);
    CHECK(std::strlen(parsed.players[1].name) == static_cast<std::size_t>(net::kNameMax));
    CHECK(std::strlen(parsed.code) == static_cast<std::size_t>(net::kCodeLength));
}

TEST_CASE("web snapshot: rejoin, Reconnecting, away players and the errors added for rejoin") {
    // A guest getting its seat back during a run: the old room is still shown, one player is
    // away, and another has come back once.
    const char json[] =
        "{\"phase\":\"Reconnecting\",\"phaseIndex\":6,\"wake\":{\"state\":\"awake\",\"ms\":0,\"attempts\":1},\"error\":\"\",\"errorIndex\":-1,"
        "\"errorCode\":\"\",\"code\":\"K7QXM\",\"myId\":1,\"hostId\":0,\"isHost\":false,\"started\":true,\"players\":["
        "{\"id\":0,\"name\":\"Ana\",\"ready\":true,\"isHost\":true,\"isMe\":false,\"transport\":\"none\",\"rttMs\":-1,\"away\":false,\"returns\":0},"
        "{\"id\":1,\"name\":\"Ben\",\"ready\":true,\"isHost\":false,\"isMe\":true,\"transport\":\"local\",\"rttMs\":-1,\"away\":false,\"returns\":0},"
        "{\"id\":2,\"name\":\"Cleo\",\"ready\":true,\"isHost\":false,\"isMe\":false,\"transport\":\"none\",\"rttMs\":-1,\"away\":true,\"returns\":0},"
        "{\"id\":3,\"name\":\"Dev\",\"ready\":true,\"isHost\":false,\"isMe\":false,\"transport\":\"none\",\"rttMs\":-1,\"away\":false,\"returns\":2}],"
        "\"serverRttMs\":-1,\"forceRelay\":false,\"rejoin\":\"K7QXM\",\"reconnecting\":true}";
    net::Lobby lobby = parse(json);
    CHECK(lobby.phase == net::LobbyPhase::Reconnecting);
    CHECK(lobby.count == 4);
    CHECK_FALSE(lobby.players[1].away);
    CHECK(lobby.players[2].away);
    CHECK(lobby.players[3].returns == 2);
    CHECK(lobby.players[0].returns == 0);
    char code[net::kCodeLength + 1] = "XXXXX";
    net::readWebRejoin(json, code);
    CHECK(std::strcmp(code, "K7QXM") == 0);

    // No saved seat, a damaged one, or a name that looks like the key: no code.
    net::readWebRejoin(kGuestInRoom, code);
    CHECK(code[0] == '\0');
    net::readWebRejoin("{\"phaseIndex\":0,\"players\":[],\"rejoin\":\"K7Q\"}", code);
    CHECK(code[0] == '\0');
    net::readWebRejoin("{\"phaseIndex\":0,\"players\":[{\"id\":0,\"name\":\"x\",\"rejoin\":\"K7QXM\"}],\"rejoin\":\"\"}", code);
    CHECK(code[0] == '\0');
    net::readWebRejoin(nullptr, code);
    CHECK(code[0] == '\0');

    // The errors added on the end of the script's list.
    const net::LobbyError added[] = {net::LobbyError::RunEnded, net::LobbyError::RoomClosed, net::LobbyError::Replaced};
    for (int i = 0; i < 3; ++i) {
        char text[512];
        std::snprintf(text, sizeof(text),
                      "{\"phase\":\"Error\",\"phaseIndex\":5,\"wake\":{\"state\":\"awake\",\"ms\":0,\"attempts\":1},\"error\":\"x\",\"errorIndex\":%d,"
                      "\"errorCode\":\"y\",\"code\":\"\",\"myId\":-1,\"hostId\":-1,\"isHost\":false,\"started\":false,\"players\":[],"
                      "\"serverRttMs\":-1,\"forceRelay\":false,\"rejoin\":\"\",\"reconnecting\":false}",
                      8 + i);
        CHECK(parse(text).error == added[i]);
    }
}
