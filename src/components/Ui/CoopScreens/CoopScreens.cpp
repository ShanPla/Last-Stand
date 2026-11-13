#include "components/Ui/CoopScreens/CoopScreens.h"

#include "components/Net/Net.h"
#include "components/Ui/Widgets/Widgets.h"
#include "core/Platform.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ls::ui {

namespace {

enum class Sub { Menu, Host, Join };

// Focus orders. The name comes first because it is first on screen, but the next one is
// selected when the menu opens, so one Enter still starts the usual thing: Rejoin when there
// is a run to go back to (it is only shown then), and Host otherwise. The menu's own order is
// worked out each frame in MenuOrder, because Rejoin comes and goes.
enum MenuFocus { kMenuName = 0, kMenuFirst = 1 };
enum HostFocus { kRoomCopy = 0, kRoomReady, kRoomStart, kRoomLeave };
enum JoinFocus { kJoinCode = 0, kJoinJoin, kJoinRetry, kJoinBack };

constexpr float kCopiedSeconds = 1.6f;
constexpr float kPanelMax = 480.0f;  // Widest a single-column screen gets, in design units.

Sub g_sub = Sub::Menu;
Nav g_menuNav;
Nav g_hostNav;
Nav g_joinNav;
TextEntry g_name;
TextEntry g_code;
char g_notice[80] = "";
bool g_wasInRoom = false;  // The lobby was live, so an error afterwards means a connection was lost.
bool g_rejoining = false;  // The waiting view is for Rejoin, not Host, so it says so.
float g_copiedAt = -100.0f;

// ---- Small drawing helpers ------------------------------------------------------

// Draws wrapped text inside the box that starts at x and is width wide, whatever the alignment.
// Returns how tall the block is, so the caller can stack things under it.
float paragraph(const char* string, float x, float y, float width, float size, Color color, font::Align align, int maxLines, bool draw = true) {
    const float lineHeight = size * 1.28f;
    const float anchor = align == font::Align::Center ? x + width * 0.5f : (align == font::Align::Right ? x + width : x);
    const int lines = font::wrapped(string, anchor, y, width, size, lineHeight, tint(color), align, maxLines, draw);
    return static_cast<float>(lines) * lineHeight;
}

// A ring of squares with one bright head and a fading tail, turning. It is shown while we wait
// for the server, so the player can see that something is still happening.
void drawSpinner(float cx, float cy, float size) {
    constexpr int kDots = 8;
    const float radius = size * 0.5f;
    const float dot = std::max(layout().px * 2.0f, size * 0.2f);
    const int head = static_cast<int>(uiSeconds() * 9.0f) % kDots;
    for (int i = 0; i < kDots; ++i) {
        const float angle = static_cast<float>(i) * 6.2831853f / static_cast<float>(kDots) - 1.5707963f;
        const int behind = (head - i + kDots) % kDots;  // 0 is the head, then 1, 2 ... along the tail.
        const Color color = behind == 0 ? theme::kCoopSpinner : (behind <= 2 ? withAlpha(theme::kCoopSpinner, 0.55f) : theme::kCoopSpinnerDim);
        pixelRect(Rectangle{cx + std::cos(angle) * radius - dot * 0.5f, cy + std::sin(angle) * radius - dot * 0.5f, dot, dot}, color);
    }
}

// A small tag such as HOST or YOU. Returns its left edge, so the next one can sit beside it.
float drawTag(const char* label, Color color, float rightX, float cy, float height) {
    const float size = height * 0.55f;
    const float width = font::measure(label, size, layout().px) + height * 0.7f;
    const Rectangle rect{rightX - width, cy - height * 0.5f, width, height};
    panel(rect, theme::kKeyChipFill, color, 0.0f);
    textMiddle(label, rect.x + width * 0.5f, cy, size, color, font::Align::Center, layout().px);
    return rect.x;
}

const char* errorText(net::LobbyError error) {
    switch (error) {
        case net::LobbyError::ServerUnreachable: return "Could not reach the server. Check your connection and try again.";
        case net::LobbyError::NotFound: return "No room has that code. Check it and try again.";
        case net::LobbyError::Full: return "That room is full.";
        case net::LobbyError::Started: return "That run has already started.";
        case net::LobbyError::BadName: return "That name is not allowed. Change it on the co-op menu.";
        case net::LobbyError::HostLeft: return "The host left. The run has ended.";
        case net::LobbyError::Version: return "This game is out of date. Reload the page and try again.";
        case net::LobbyError::Other: return "Something went wrong. Please try again.";
        case net::LobbyError::RunEnded: return "That run has ended.";
        case net::LobbyError::RoomClosed: return "Room closed. That run has ended.";
        case net::LobbyError::Replaced: return "You went back into the run from another tab.";
        case net::LobbyError::None: return "";
    }
    return "";
}

void setNotice(const char* message) { std::snprintf(g_notice, sizeof(g_notice), "%s", message); }

// A name for the request: what was typed, or Player N, which is the protocol's suggestion.
void effectiveName(bool host, char* out, size_t size) {
    if (g_name.length > 0) std::snprintf(out, size, "%s", g_name.text);
    else std::snprintf(out, size, "%s", host ? "Player 1" : "Player 2");
}

void goTo(Sub sub) {
    textCancel();
    g_sub = sub;
    g_hostNav = Nav{};
    g_joinNav = Nav{};
    if (sub == Sub::Host) g_hostNav.selected = kRoomReady;
    if (sub == Sub::Join) g_joinNav.selected = kJoinCode;
    if (sub == Sub::Menu) {
        g_menuNav = Nav{};
        g_menuNav.selected = kMenuFirst;
        g_rejoining = false;
    }
}

// The name is kept when its editing ends. An empty one is fine and means Player N.
bool commitName(SaveData& data) {
    char before[sizeof(data.coopName)];
    std::snprintf(before, sizeof(before), "%s", data.coopName);
    std::snprintf(data.coopName, sizeof(data.coopName), "%s", g_name.length > 0 && net::validName(g_name.text) ? g_name.text : "");
    return std::strcmp(before, data.coopName) != 0;
}

// ---- What the waiting views say -------------------------------------------------

struct Status {
    const char* head = "";
    const char* sub = "";
    Color color = theme::kUiText;
    bool spin = false;
    bool canRetry = false;
};

Status statusFor(const net::Lobby& lobby, bool hosting) {
    Status status;
    switch (lobby.phase) {
        case net::LobbyPhase::Waking:
            status.head = "Waking the server...";
            status.spin = true;
            if (lobby.wakeSeconds >= net::kWakeRetrySeconds) {
                status.sub = "This is taking longer than usual.";
                status.canRetry = true;
            } else {
                status.sub = "A free server can take about a minute to start.";
            }
            break;
        case net::LobbyPhase::Connecting:
            status.head = g_rejoining ? "Rejoining the run..." : (hosting ? "Creating your room..." : "Joining the room...");
            status.spin = true;
            break;
        case net::LobbyPhase::Reconnecting:
            status.head = "Reconnecting...";
            status.sub = "The connection dropped. Getting your place back.";
            status.spin = true;
            break;
        case net::LobbyPhase::Error:
            status.head = errorText(lobby.error);
            status.color = theme::kCoopError;
            status.canRetry = lobby.error == net::LobbyError::ServerUnreachable || lobby.error == net::LobbyError::Other;
            break;
        case net::LobbyPhase::Starting:
            status.head = "Starting the run...";
            status.spin = true;
            break;
        case net::LobbyPhase::Offline:
        case net::LobbyPhase::InLobby:
            status.head = "Not connected.";
            status.color = theme::kUiTextDim;
            break;
    }
    return status;
}

void drawBackdrop() { pixelRect(Rectangle{0.0f, 0.0f, layout().w, layout().h}, theme::kScreenScrim); }

// ---- Menu ----------------------------------------------------------------------

// Where each menu button sits in the focus order. Rejoin is only there while a run can be
// rejoined, and the others move down one when it is.
struct MenuOrder {
    bool rejoin = false;
    int rejoinFocus = -1;
    int host = kMenuFirst;
    int join = kMenuFirst + 1;
    int back = kMenuFirst + 2;
    int count = kMenuFirst + 3;
};

MenuOrder menuOrder() {
    MenuOrder order;
    order.rejoin = net::canRejoin();
    if (order.rejoin) {
        order.rejoinFocus = kMenuFirst;
        order.host++;
        order.join++;
        order.back++;
        order.count++;
    }
    return order;
}

CoopResult drawMenu(SaveData& data) {
    const Layout& l = layout();
    const float u = l.u;
    CoopResult result;
    const bool keys = textKeyboardOpen();
    setInputEnabled(!keys);
    drawBackdrop();
    const MenuOrder order = menuOrder();

    const float panelWidth = std::min(l.width(), kPanelMax * u);
    const float x = l.centerX() - panelWidth * 0.5f;
    const float titleHeight = 56.0f * u;
    const float subtitleHeight = 28.0f * u;
    const float fieldHeight = tall(58.0f * u);
    const float buttonHeight = tall(54.0f * u);
    const float gap = 9.0f * u;
    const bool hasNotice = g_notice[0] != '\0';
    const float noticePad = 10.0f * u;
    const float noticeTextHeight = hasNotice ? paragraph(g_notice, 0.0f, 0.0f, panelWidth - noticePad * 2.0f, 18.0f * u, theme::kCoopNotice, font::Align::Left, 3, false) : 0.0f;
    const float noticeHeight = hasNotice ? noticeTextHeight + noticePad * 2.0f + gap : 0.0f;
    const bool waking = net::lobby().phase == net::LobbyPhase::Waking;
    const float statusHeight = waking ? 30.0f * u : 0.0f;
    const float above = titleHeight + subtitleHeight + noticeHeight + fieldHeight + gap + statusHeight;
    float content = above + (buttonHeight + gap) * (order.rejoin ? 4.0f : 3.0f);
    // On a very short window the extra Rejoin row may not fit. Host and Join then share one
    // row, side by side, so every button stays on screen at its full touch height.
    const bool pairRow = order.rejoin && content > l.height();
    if (pairRow) content = above + (buttonHeight + gap) * 3.0f;
    const float pairWidth = (panelWidth - gap) * 0.5f;
    float y = l.top + std::max(0.0f, (l.height() - content) * 0.5f);

    textShadowed("CO-OP", l.centerX(), y, 44.0f * u, theme::kUiAccent, font::Align::Center, l.px);
    y += titleHeight;
    text("Up to 4 players. Share a code to play together.", l.centerX(), y - 8.0f * u, font::fitSize("Up to 4 players. Share a code to play together.", 18.0f * u, panelWidth), theme::kUiTextDim, font::Align::Center);
    y += subtitleHeight;

    if (hasNotice) {
        const Rectangle box{x, y, panelWidth, noticeHeight - gap};
        panel(box, theme::kUiPanel, theme::kCoopNotice, 0.0f);
        paragraph(g_notice, x + noticePad, y + noticePad, panelWidth - noticePad * 2.0f, 18.0f * u, theme::kCoopNotice, font::Align::Left, 3);
        registerExtent("coop notice", box);
        y += noticeHeight;
    }

    if (!keys) navigate(g_menuNav, order.count);
    if (g_menuNav.selected >= order.count) g_menuNav.selected = order.count - 1;

    // Pasted text goes to the name field, the only field on this screen.
    if (input().pastedLength > 0 && !textEditing()) g_menuNav.selected = kMenuName;
    textField(g_menuNav, kMenuName, Rectangle{x, y, panelWidth, fieldHeight}, "Your name", g_name, TextKind::Name, "Optional. Empty means Player N.");
    y += fieldHeight + gap;

    if (order.rejoin) {
        // The run this player was in, after a reload or a closed tab. It goes first and is the
        // main button, because it is almost always what someone back on this menu wants.
        char label[32];
        std::snprintf(label, sizeof(label), "Rejoin %s", net::rejoinCode());
        if (button(g_menuNav, order.rejoinFocus, Rectangle{x, y, panelWidth, buttonHeight}, label, ButtonLook::Primary)) {
            if (commitName(data)) result.saveChanged = true;
            g_notice[0] = '\0';
            g_wasInRoom = false;
            net::rejoinRoom();
            goTo(Sub::Host);
            g_rejoining = true;
            return result;
        }
        y += buttonHeight + gap;
    }
    if (button(g_menuNav, order.host, Rectangle{x, y, pairRow ? pairWidth : panelWidth, buttonHeight}, "Host Co-op",
               order.rejoin ? ButtonLook::Normal : ButtonLook::Primary)) {
        if (commitName(data)) result.saveChanged = true;
        char name[net::kNameMax + 1];
        effectiveName(true, name, sizeof(name));
        g_notice[0] = '\0';
        g_wasInRoom = false;
        net::hostRoom(name);
        goTo(Sub::Host);
        return result;
    }
    if (!pairRow) y += buttonHeight + gap;
    const Rectangle joinRect = pairRow ? Rectangle{x + pairWidth + gap, y, pairWidth, buttonHeight} : Rectangle{x, y, panelWidth, buttonHeight};
    if (button(g_menuNav, order.join, joinRect, "Join Co-op")) {
        if (commitName(data)) result.saveChanged = true;
        g_notice[0] = '\0';
        g_wasInRoom = false;
        net::leaveRoom();  // Clears a leftover error, so the join screen starts clean.
        textSet(g_code, TextKind::Code, "");
        goTo(Sub::Join);
        return result;
    }
    y += buttonHeight + gap;
    const bool backPressed = button(g_menuNav, order.back, Rectangle{x, y, panelWidth, buttonHeight}, "Back", ButtonLook::Normal, promptBack());
    y += buttonHeight + gap;

    if (waking) {
        drawSpinner(l.centerX() - panelWidth * 0.5f + 12.0f * u, y + 10.0f * u, 18.0f * u);
        text("Waking the server...", x + 32.0f * u, y, 18.0f * u, theme::kUiTextDim);
        registerExtent("coop status", Rectangle{x, y - 2.0f * u, panelWidth, 24.0f * u});
    }

    drawTextKeyboard();
    if (g_name.finished && commitName(data)) result.saveChanged = true;

    // Esc and B step back out. A field that was being edited used the press up already.
    if (!keys && (backPressed || (input().back && !textEditing()))) {
        if (commitName(data)) result.saveChanged = true;
        result.back = true;
    }
    return result;
}

// ---- Host screen and lobby view --------------------------------------------------

// One line of the player list.
void drawPlayerRow(Rectangle row, int slot, const net::Lobby& lobby) {
    const float u = layout().u;
    const float p = layout().px;
    const bool filled = slot < lobby.count;
    char label[32];
    std::snprintf(label, sizeof(label), "coop player %d", slot);
    registerExtent(label, row);
    panel(row, withAlpha(theme::kUiPanel, filled ? 1.0f : 0.55f), filled ? theme::kUiBorder : theme::kDivider, 0.0f);

    const float cy = row.y + row.height * 0.5f;
    const float swatch = std::min(row.height - 8.0f * p, 26.0f * u);
    const Rectangle swatchRect{row.x + 5.0f * p, cy - swatch * 0.5f, swatch, swatch};
    char number[4];
    std::snprintf(number, sizeof(number), "%d", slot + 1);
    const float nameX = swatchRect.x + swatch + 6.0f * p;
    const float nameSize = std::min(row.height * 0.5f, 22.0f * u);

    if (!filled) {
        panel(swatchRect, theme::kCoopCodePlate, theme::kDivider, 0.0f);
        textMiddle(number, swatchRect.x + swatch * 0.5f, cy, swatch * 0.55f, theme::kUiTextDim, font::Align::Center);
        textMiddle("Waiting for a player...", nameX, cy, font::fitSize("Waiting for a player...", nameSize, row.width - (nameX - row.x) - 6.0f * p), theme::kUiTextDim);
        return;
    }
    const net::LobbyPlayer& player = lobby.players[slot];
    panel(swatchRect, theme::kCoopSlotColors[slot], theme::kUiBorderDark, 0.0f);
    textMiddle(number, swatchRect.x + swatch * 0.5f, cy, swatch * 0.55f, theme::kTextOnPrimary, font::Align::Center);

    // From the right edge inward: ready mark, its word when there is room, then the tags.
    float right = row.x + row.width - 6.0f * p;
    drawCheckBox(right - p * 4.5f, cy, p, player.ready);
    right -= p * 9.0f + 6.0f * p;
    const float tagHeight = std::min(row.height * 0.6f, 22.0f * u);
    // The word beside the mark only shows when the name, the tags and the word all fit. The mark
    // alone is enough to read the state.
    // In a started run someone whose connection is gone is shown as away: they may come back.
    const char* word = player.away ? "AWAY" : (player.ready ? "READY" : "WAITING");
    const float wordSize = std::min(row.height * 0.38f, 16.0f * u);
    const float tagsWidth = (slot == lobby.you ? font::measure("YOU", tagHeight * 0.55f, p) + tagHeight * 0.7f + 4.0f * p : 0.0f) +
                            (slot == lobby.host ? font::measure("HOST", tagHeight * 0.55f, p) + tagHeight * 0.7f + 4.0f * p : 0.0f);
    const float wordWidth = font::measure(word, wordSize, p) + 8.0f * p;
    if (right - nameX - tagsWidth - wordWidth >= font::measure(player.name, nameSize)) {
        textMiddle(word, right, cy, wordSize, player.ready && !player.away ? theme::kCoopReady : theme::kCoopWaiting, font::Align::Right, p);
        right -= wordWidth;
    }
    if (slot == lobby.you) right = drawTag("YOU", theme::kCoopYou, right, cy, tagHeight) - 4.0f * p;
    if (slot == lobby.host) right = drawTag("HOST", theme::kCoopHost, right, cy, tagHeight) - 4.0f * p;
    const float room = right - nameX;
    textMiddle(player.name, nameX, cy, font::fitSize(player.name, nameSize, room), theme::kUiText);
}

// The code, big, in its plate with the Copy button beside it.
void drawCodePlate(Rectangle plate, const net::Lobby& lobby, int copyFocus, Nav& nav) {
    const float u = layout().u;
    const float p = layout().px;
    panel(plate, theme::kCoopCodePlate, theme::kUiBorder, 0.0f);
    registerExtent("coop code", plate);
    const float pad = 10.0f * u;
    const float copyHeight = std::min(plate.height - pad * 1.2f, std::max(tall(40.0f * u), 0.0f));
    const float copyWidth = std::max(84.0f * u, 2.2f * copyHeight);
    const bool copied = uiSeconds() - g_copiedAt < kCopiedSeconds;
    const Rectangle copyRect{plate.x + plate.width - pad - copyWidth, plate.y + (plate.height - copyHeight) * 0.5f, copyWidth, copyHeight};
    text("ROOM CODE", plate.x + pad, plate.y + pad * 0.7f, 13.0f * u, theme::kUiTextDim, font::Align::Left, p);
    const float codeRoom = copyRect.x - (plate.x + pad) - pad;
    // Letters spaced apart, so a code read aloud is easy to follow.
    const float codeSize = font::fitSize(lobby.code, std::min(plate.height * 0.5f, 42.0f * u), codeRoom, p * 3.0f);
    // One character at a time, so digits get their own color. The font draws Z and 2 almost the
    // same, and a code is read out or copied by eye.
    const float spacing = p * 3.0f;
    const float codeY = plate.y + plate.height * 0.66f;
    const std::size_t length = std::strlen(lobby.code);
    for (std::size_t i = 0; i < length; ++i) {
        char before[net::kCodeLength + 1] = {};
        std::memcpy(before, lobby.code, i);
        const char one[2] = {lobby.code[i], '\0'};
        const bool digit = lobby.code[i] >= '0' && lobby.code[i] <= '9';
        textMiddle(one, plate.x + pad + font::measure(before, codeSize, spacing), codeY, codeSize, digit ? theme::kCoopCodeDigit : theme::kCoopCode, font::Align::Left, spacing);
    }
    if (button(nav, copyFocus, copyRect, copied ? "Copied" : "Copy", ButtonLook::Normal)) {
        platform::copyToClipboard(lobby.code);
        g_copiedAt = uiSeconds();
    }
}

void drawWaitingView(Nav& nav, bool hosting, const char* cancelLabel, bool* retry, bool* cancel) {
    const Layout& l = layout();
    const float u = l.u;
    const net::Lobby& lobby = net::lobby();
    const Status status = statusFor(lobby, hosting);
    const float panelWidth = std::min(l.width(), kPanelMax * u);
    const float x = l.centerX() - panelWidth * 0.5f;
    const float titleHeight = 56.0f * u;
    const float spinnerHeight = status.spin ? 64.0f * u : 0.0f;
    const float headHeight = paragraph(status.head, 0.0f, 0.0f, panelWidth, 24.0f * u, status.color, font::Align::Center, 3, false);
    const float subHeight = status.sub[0] ? paragraph(status.sub, 0.0f, 0.0f, panelWidth, 17.0f * u, theme::kUiTextDim, font::Align::Center, 2, false) + 6.0f * u : 0.0f;
    const float elapsedHeight = lobby.phase == net::LobbyPhase::Waking ? 26.0f * u : 0.0f;
    const float buttonHeight = tall(54.0f * u);
    const float gap = 9.0f * u;
    const int buttons = status.canRetry ? 2 : 1;
    const float content = titleHeight + spinnerHeight + headHeight + subHeight + elapsedHeight + 16.0f * u + (buttonHeight + gap) * static_cast<float>(buttons);
    float y = l.top + std::max(0.0f, (l.height() - content) * 0.5f);

    textShadowed(hosting ? "HOST CO-OP" : "CO-OP", l.centerX(), y, 44.0f * u, theme::kUiAccent, font::Align::Center, l.px);
    y += titleHeight;
    if (status.spin) {
        drawSpinner(l.centerX(), y + spinnerHeight * 0.5f, 40.0f * u);
        y += spinnerHeight;
    }
    const float headTop = y;
    paragraph(status.head, x, y, panelWidth, 24.0f * u, status.color, font::Align::Center, 3);
    y += headHeight;
    if (status.sub[0]) {
        y += 6.0f * u;
        paragraph(status.sub, x, y, panelWidth, 17.0f * u, theme::kUiTextDim, font::Align::Center, 2);
        y += subHeight - 6.0f * u;
    }
    if (elapsedHeight > 0.0f) {
        char elapsed[32];
        std::snprintf(elapsed, sizeof(elapsed), "%d s", static_cast<int>(lobby.wakeSeconds));
        text(elapsed, l.centerX(), y + 4.0f * u, 18.0f * u, theme::kUiTextDim, font::Align::Center);
        y += elapsedHeight;
    }
    registerExtent("coop status", Rectangle{x, headTop, panelWidth, y - headTop});
    y += 16.0f * u;

    navigate(nav, buttons);
    if (status.canRetry) {
        if (button(nav, 0, Rectangle{x, y, panelWidth, buttonHeight}, "Retry", ButtonLook::Primary)) *retry = true;
        y += buttonHeight + gap;
    }
    if (button(nav, buttons - 1, Rectangle{x, y, panelWidth, buttonHeight}, cancelLabel, ButtonLook::Normal, promptBack())) *cancel = true;
    if (input().back) *cancel = true;
}

// The room: code, players, and the buttons. A host sees Start, a joined player does not.
void drawRoomView(Nav& nav, const net::Lobby& lobby, bool* leave) {
    const Layout& l = layout();
    const float u = l.u;
    const bool isHost = lobby.you == lobby.host;
    const bool starting = lobby.phase == net::LobbyPhase::Starting;
    const bool wide = !l.portrait && l.width() >= 700.0f * u;
    const float gap = 9.0f * u;
    const float buttonHeight = tall(50.0f * u);
    const float titleHeight = 56.0f * u;
    const float plateHeight = std::max(70.0f * u, l.touch + 24.0f * u);
    const float rowGap = std::max(l.px * 2.0f, 4.0f * u);
    const float minRow = std::max(0.62f * l.touch, 30.0f * u);
    const float maxRow = std::max(minRow, 40.0f * u);

    const float panelWidth = wide ? std::min(l.width(), 960.0f * u) : std::min(l.width(), kPanelMax * u);
    const float x = l.centerX() - panelWidth * 0.5f;
    const float leftWidth = wide ? panelWidth * 0.58f : panelWidth;
    const float rightX = x + leftWidth + gap * 2.0f;
    const float rightWidth = wide ? std::min(x + panelWidth - rightX, 340.0f * u) : panelWidth;

    // The list takes the height that is left, between a minimum and a maximum.
    const int slots = net::kMaxPlayers;
    const float buttonsTall = wide ? 0.0f : (buttonHeight + gap) * (isHost ? 3.0f : 2.0f) + 28.0f * u;
    const float fixed = titleHeight + plateHeight + gap + buttonsTall;
    float rowHeight = (l.height() - fixed - rowGap * static_cast<float>(slots - 1)) / static_cast<float>(slots);
    rowHeight = std::min(maxRow, std::max(minRow, rowHeight));
    const float listHeight = rowHeight * static_cast<float>(slots) + rowGap * static_cast<float>(slots - 1);
    const float content = titleHeight + plateHeight + gap + listHeight + (wide ? 0.0f : gap + buttonsTall - gap);
    float y = l.top + std::max(0.0f, (l.height() - content) * 0.5f);

    const float titleY = y;
    textShadowed(isHost ? "YOUR ROOM" : "CO-OP LOBBY", wide ? x + leftWidth * 0.5f : l.centerX(), titleY, 44.0f * u, theme::kUiAccent, font::Align::Center, l.px);
    y += titleHeight;

    const bool startEnabled = net::canStart();
    // Selection never rests on a disabled Start.
    const int leaveFocus = isHost ? kRoomLeave : kRoomLeave - 1;
    navigate(nav, leaveFocus + 1);
    if (isHost && !startEnabled && nav.selected == kRoomStart) nav.selected = input().stepY < 0 ? kRoomReady : kRoomLeave;
    if (nav.selected > leaveFocus) nav.selected = leaveFocus;

    drawCodePlate(Rectangle{x, y, leftWidth, plateHeight}, lobby, kRoomCopy, nav);
    y += plateHeight + gap;

    const float listTop = y;
    for (int slot = 0; slot < slots; ++slot) {
        drawPlayerRow(Rectangle{x, listTop + static_cast<float>(slot) * (rowHeight + rowGap), leftWidth, rowHeight}, slot, lobby);
    }
    y = listTop + listHeight;

    // One line that says what the lobby is waiting for. It is dropped when a short window has no room.
    const char* hint = isHost ? (startEnabled ? "Everyone is ready. Press Start." : (lobby.count < 2 ? "Waiting for another player to join." : "Waiting for everyone to be ready."))
                              : "Waiting for the host to start.";
    const float bx = wide ? rightX : x;
    const float bw = wide ? rightWidth : panelWidth;
    const float hintHeight = paragraph(hint, bx, 0.0f, bw, 16.0f * u, theme::kUiTextDim, font::Align::Center, 2, false);
    const float buttonsHeight = (buttonHeight + gap) * (isHost ? 3.0f : 2.0f);

    // Buttons: under the list on a narrow screen, in a column beside it on a wide one.
    float by = wide ? std::max(l.top + titleHeight, l.top + (l.height() - buttonsHeight - hintHeight) * 0.5f) : y + gap;

    bool ready = lobby.you >= 0 && lobby.you < lobby.count && lobby.players[lobby.you].ready;
    if (starting) {
        drawSpinner(bx + 14.0f * u, by + 12.0f * u, 22.0f * u);
        text("Starting the run...", bx + 34.0f * u, by + 2.0f * u, 20.0f * u, theme::kUiText);
        registerExtent("coop status", Rectangle{bx, by, bw, 26.0f * u});
        return;
    }
    if (checkBox(nav, kRoomReady, Rectangle{bx, by, bw, buttonHeight}, "Ready", &ready)) net::setReady(ready);
    by += buttonHeight + gap;
    if (isHost) {
        if (button(nav, kRoomStart, Rectangle{bx, by, bw, buttonHeight}, "Start", ButtonLook::Primary, nullptr, startEnabled)) net::startRun();
        by += buttonHeight + gap;
    }
    if (button(nav, leaveFocus, Rectangle{bx, by, bw, buttonHeight}, "Leave", ButtonLook::Normal, promptBack())) *leave = true;
    by += buttonHeight + gap * 0.5f;

    if (by + hintHeight <= l.bottom) {
        paragraph(hint, bx, by, bw, 16.0f * u, startEnabled ? theme::kCoopReady : theme::kUiTextDim, font::Align::Center, 2);
        registerExtent("coop hint", Rectangle{bx, by, bw, hintHeight});
    }
    if (input().back) *leave = true;
}

// Watches for the room closing or the connection dropping, and returns to the menu with a note.
// Returns true when it did.
bool checkRoomLost() {
    const net::Lobby& lobby = net::lobby();
    if (lobby.phase == net::LobbyPhase::InLobby || lobby.phase == net::LobbyPhase::Starting) g_wasInRoom = true;
    if (lobby.phase != net::LobbyPhase::Error) return false;
    if (lobby.error == net::LobbyError::HostLeft) {
        setNotice("The host left. The run has ended.");
    } else if (lobby.error == net::LobbyError::RunEnded || lobby.error == net::LobbyError::RoomClosed || lobby.error == net::LobbyError::Replaced) {
        // A refused rejoin, or a run that went on without this player. The saved seat is gone
        // already (except for Replaced, where the other tab has it), so the menu shows no Rejoin.
        setNotice(errorText(lobby.error));
    } else if (g_wasInRoom && lobby.error == net::LobbyError::ServerUnreachable) {
        setNotice("Lost the connection to the server.");
    } else {
        return false;
    }
    net::leaveRoom();
    g_wasInRoom = false;
    goTo(Sub::Menu);
    return true;
}

CoopResult drawHost() {
    drawBackdrop();
    if (checkRoomLost()) return CoopResult{};
    const net::Lobby& lobby = net::lobby();
    bool leave = false;
    if (lobby.phase == net::LobbyPhase::InLobby || lobby.phase == net::LobbyPhase::Starting) {
        drawRoomView(g_hostNav, lobby, &leave);
        if (leave) {
            net::leaveRoom();
            goTo(Sub::Menu);
        }
        return CoopResult{};
    }
    bool retry = false;
    bool cancel = false;
    const bool failed = lobby.phase == net::LobbyPhase::Error;
    drawWaitingView(g_hostNav, !g_rejoining, failed ? "Back" : "Cancel", &retry, &cancel);
    if (retry) net::retry();
    if (cancel) {
        net::leaveRoom();
        goTo(Sub::Menu);
    }
    return CoopResult{};
}

// ---- Join screen -----------------------------------------------------------------

CoopResult drawJoin(SaveData& data) {
    const Layout& l = layout();
    const float u = l.u;
    CoopResult result;
    (void)data;
    const bool keys = textKeyboardOpen();
    setInputEnabled(!keys);
    drawBackdrop();

    const net::Lobby& lobby = net::lobby();
    // A joined room is shown as the lobby.
    if (lobby.phase == net::LobbyPhase::InLobby || lobby.phase == net::LobbyPhase::Starting) {
        textCancel();
        g_wasInRoom = true;
        goTo(Sub::Host);
        return result;
    }

    const bool busy = lobby.phase == net::LobbyPhase::Waking || lobby.phase == net::LobbyPhase::Connecting;
    const Status status = statusFor(lobby, false);
    const bool showStatus = busy || lobby.phase == net::LobbyPhase::Error;

    const float panelWidth = std::min(l.width(), kPanelMax * u);
    const float x = l.centerX() - panelWidth * 0.5f;
    const float titleHeight = 56.0f * u;
    const float fieldHeight = tall(58.0f * u);
    const float buttonHeight = tall(54.0f * u);
    const float gap = 9.0f * u;
    const float hintHeight = 26.0f * u;
    const float statusTextWidth = panelWidth - (status.spin ? 34.0f * u : 0.0f);
    const float statusText = showStatus ? paragraph(status.head, 0.0f, 0.0f, statusTextWidth, 19.0f * u, status.color, font::Align::Left, 3, false) +
                                              (status.sub[0] ? paragraph(status.sub, 0.0f, 0.0f, statusTextWidth, 15.0f * u, theme::kUiTextDim, font::Align::Left, 2, false) + 4.0f * u : 0.0f)
                                        : 0.0f;
    const float statusHeight = showStatus ? std::max(statusText, 28.0f * u) + gap : 0.0f;
    const bool showRetry = showStatus && status.canRetry;
    const float content = titleHeight + fieldHeight + hintHeight + statusHeight + (buttonHeight + gap) * (showRetry ? 3.0f : 2.0f);
    float y = l.top + std::max(0.0f, (l.height() - content) * 0.5f);

    textShadowed("JOIN CO-OP", l.centerX(), y, 44.0f * u, theme::kUiAccent, font::Align::Center, l.px);
    y += titleHeight;

    if (!keys) navigate(g_joinNav, showRetry ? 4 : 3);
    // The Retry slot only exists when shown, so the Back index moves with it.
    const int backFocus = showRetry ? kJoinBack : kJoinBack - 1;
    if (g_joinNav.selected > backFocus) g_joinNav.selected = backFocus;

    // Pasted text goes to the room code field, wherever the selection is, since a code is what people paste here.
    const bool pastedCode = input().pastedLength > 0 && !textEditing();
    if (pastedCode) g_joinNav.selected = kJoinCode;
    textField(g_joinNav, kJoinCode, Rectangle{x, y, panelWidth, fieldHeight}, "Room code", g_code, TextKind::Code, "5 characters, such as K7QXM");
    // A whole code pasted in: the next Enter joins.
    if (pastedCode && net::validCode(g_code.text)) g_joinNav.selected = kJoinJoin;
    y += fieldHeight;
    text("Ask the host for their room code.", x, y + 4.0f * u, font::fitSize("Ask the host for their room code.", 16.0f * u, panelWidth), theme::kUiTextDim);
    y += hintHeight;

    if (showStatus) {
        const float top = y;
        float ty = y;
        const float tx = x + (status.spin ? 34.0f * u : 0.0f);
        if (status.spin) drawSpinner(x + 12.0f * u, y + 12.0f * u, 20.0f * u);
        ty += paragraph(status.head, tx, ty, statusTextWidth, 19.0f * u, status.color, font::Align::Left, 3);
        if (status.sub[0]) ty += paragraph(status.sub, tx, ty + 4.0f * u, statusTextWidth, 15.0f * u, theme::kUiTextDim, font::Align::Left, 2) + 4.0f * u;
        registerExtent("coop status", Rectangle{x, top, panelWidth, std::max(ty - top, 28.0f * u)});
        y += statusHeight;
    }

    const bool canJoin = !busy && net::validCode(g_code.text);
    if (button(g_joinNav, kJoinJoin, Rectangle{x, y, panelWidth, buttonHeight}, "Join", ButtonLook::Primary, nullptr, canJoin)) {
        char name[net::kNameMax + 1];
        effectiveName(false, name, sizeof(name));
        g_wasInRoom = false;
        net::joinRoom(g_code.text, name);
    }
    y += buttonHeight + gap;
    bool retry = false;
    if (showRetry) {
        if (button(g_joinNav, kJoinRetry, Rectangle{x, y, panelWidth, buttonHeight}, "Retry")) retry = true;
        y += buttonHeight + gap;
    }
    const bool backPressed = button(g_joinNav, backFocus, Rectangle{x, y, panelWidth, buttonHeight}, "Back", ButtonLook::Normal, promptBack());

    drawTextKeyboard();

    // With a keyboard, Enter after typing a full code moves on to the Join button.
    if (g_code.finished && canJoin) g_joinNav.selected = kJoinJoin;
    if (retry) net::retry();
    if (!keys && (backPressed || (input().back && !textEditing()))) {
        net::leaveRoom();  // Cancels a join in progress too.
        textSet(g_code, TextKind::Code, "");
        goTo(Sub::Menu);
    }
    return result;
}

}  // namespace

void openCoopScreens(const SaveData& data) {
    textSet(g_name, TextKind::Name, data.coopName);
    goTo(Sub::Menu);
}

CoopResult drawCoopScreens(SaveData& data) {
    switch (g_sub) {
        case Sub::Menu: return drawMenu(data);
        case Sub::Host: return drawHost();
        case Sub::Join: return drawJoin(data);
    }
    return CoopResult{};
}

const char* coopScreenName() {
    switch (g_sub) {
        case Sub::Menu: return "coop-menu";
        case Sub::Host: return "coop-host";
        case Sub::Join: return "coop-join";
    }
    return "";
}

const char* coopDebugJson() {
    static char json[2048];
    const net::Lobby& lobby = net::lobby();
    int n = std::snprintf(json, sizeof(json),
                          "{\"sub\":\"%s\",\"phase\":%d,\"error\":%d,\"code\":\"%s\",\"you\":%d,\"host\":%d,\"started\":%d,\"count\":%d,"
                          "\"canStart\":%d,\"wake\":%.1f,\"name\":\"%s\",\"codeText\":\"%s\",\"editing\":%d,\"keys\":%d,\"notice\":\"%s\","
                          "\"serverRtt\":%d,\"rejoin\":\"%s\",\"rejoining\":%d,\"players\":[",
                          coopScreenName(), static_cast<int>(lobby.phase), static_cast<int>(lobby.error), lobby.code, lobby.you, lobby.host,
                          lobby.started ? 1 : 0, lobby.count, net::canStart() ? 1 : 0, static_cast<double>(lobby.wakeSeconds), g_name.text,
                          g_code.text, textEditing() ? 1 : 0, textKeyboardOpen() ? 1 : 0, g_notice, lobby.serverRttMs, net::rejoinCode(), g_rejoining ? 1 : 0);
    for (int i = 0; i < lobby.count && n > 0 && n < static_cast<int>(sizeof(json)) - 220; ++i) {
        const net::LobbyPlayer& player = lobby.players[i];
        n += std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n),
                           "%s{\"id\":%d,\"name\":\"%s\",\"ready\":%d,\"away\":%d,\"returns\":%d,\"transport\":\"%s\",\"rtt\":%d,\"msgsIn\":%d,\"msgsOut\":%d}",
                           i > 0 ? "," : "", player.id, player.name, player.ready ? 1 : 0, player.away ? 1 : 0, player.returns,
                           net::transportName(player.transport), player.rttMs, player.msgsInPerSec, player.msgsOutPerSec);
    }
    std::snprintf(json + n, sizeof(json) - static_cast<size_t>(n), "]}");
    return json;
}

}  // namespace ls::ui
