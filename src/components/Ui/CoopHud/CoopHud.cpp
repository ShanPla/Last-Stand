#include "components/Ui/CoopHud/CoopHud.h"

#include "components/Ui/Hud/Hud.h"
#include "components/Ui/Widgets/Widgets.h"
#include "components/WorldView/WorldView.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ls::ui {

namespace {

constexpr float kNoticeSeconds = 4.0f;
constexpr float kTagLift = 52.0f;    // Art pixels above the feet: just over the helmet.
constexpr int kNameBytes = 16;

char g_names[kMaxPlayers][kNameBytes] = {};
int g_colors[kMaxPlayers] = {0, 1, 2, 3};
char g_notice[64] = "";
char g_lastNotice[64] = "";
float g_noticeLeft = 0.0f;

Color slotColor(int slot) { return theme::kCoopSlotColors[std::clamp(g_colors[slot], 0, 3)]; }

// A name over a player's head on a dark plate, in their color, and the DOWN mark and revive
// bar under it when they are down. The tag is drawn for this screen's own player only when down.
void drawTag(const GameState& state, int slot, float u) {
    const Player& player = state.players[slot];
    const bool self = slot == state.localSlot;
    if (self && !player.downed) return;
    float x = 0.0f;
    float y = 0.0f;
    if (!worldview::screenPosition(player.pos, player.downed ? 24.0f : kTagLift, &x, &y)) return;
    const char* name = g_names[slot][0] ? g_names[slot] : "Player";
    const float size = 15.0f * u;
    const float pad = 5.0f * u;
    float top = y - size - pad * 2.0f;
    if (!self) {
        const float width = font::measure(name, size) + pad * 2.0f;
        pixelRect(Rectangle{x - width * 0.5f, top, width, size + pad * 2.0f}, theme::kCoopTagBack);
        text(name, x, top + pad, size, slotColor(slot), font::Align::Center);
    }
    if (!player.downed) return;
    // Down: a warm mark, so it reads as trouble, and a cool bar that fills while someone helps.
    top -= size + pad * 3.0f;
    const float barWidth = 64.0f * u;
    const float barHeight = 7.0f * u;
    textShadowed("DOWN", x, top, size, theme::kCoopDowned, font::Align::Center, 1.5f * u);
    const float fraction = std::clamp(player.reviveProgress / balance::kReviveSeconds, 0.0f, 1.0f);
    bar(Rectangle{x - barWidth * 0.5f, top + size + pad * 0.5f, barWidth, barHeight}, fraction, theme::kCoopReviveFill, theme::kCoopReviveBack);
}

// Four rising bars, like a phone's signal, under the pause button.
void drawSignal(const CoopHudView& view, float u) {
    const input::Rect pause = pauseButtonRect();
    const int quality = connectionQuality(view.pingMs, view.snapshotAge);
    const Color on = quality >= 3 ? theme::kCoopSignalGood : (quality == 2 ? theme::kCoopSignalFair : theme::kCoopSignalPoor);
    const float barWidth = 5.0f * u;
    const float gap = 3.0f * u;
    const float tallest = 16.0f * u;
    const float right = pause.x + pause.w - 10.0f * u;
    const float bottom = pause.y + pause.h + 6.0f * u + tallest;
    for (int i = 0; i < 4; ++i) {
        const float height = tallest * static_cast<float>(i + 1) / 4.0f;
        const float x = right - static_cast<float>(4 - i) * (barWidth + gap) + gap;
        pixelRect(Rectangle{x, bottom - height, barWidth, height}, i < quality ? on : theme::kCoopSignalOff);
    }
    char ping[16];
    if (view.pingMs >= 0) std::snprintf(ping, sizeof(ping), "%d ms", view.pingMs);
    else std::snprintf(ping, sizeof(ping), "-- ms");
    const float size = 13.0f * u;
    text(ping, right - 4.0f * (barWidth + gap) - 6.0f * u, bottom - size, size, theme::kUiTextDim, font::Align::Right);
    registerExtent("coop signal", Rectangle{right - 4.0f * (barWidth + gap) - 70.0f * u, bottom - tallest, 70.0f * u + 4.0f * (barWidth + gap), tallest});
}

void drawBanner(const char* message, float y, float u) {
    const Layout& l = layout();
    const float size = 22.0f * u;
    const float pad = 10.0f * u;
    const float width = std::min(l.width(), font::measure(message, size) + pad * 4.0f);
    const Rectangle box{l.centerX() - width * 0.5f, y, width, size + pad * 2.0f};
    pixelRect(box, theme::kCoopBannerBack);
    text(message, l.centerX(), y + pad, font::fitSize(message, size, width - pad * 2.0f), theme::kCoopBannerText, font::Align::Center);
    registerExtent("coop banner", box);
}

void drawDebug(const char* lines, float u) {
    const Layout& l = layout();
    const float size = 14.0f * u;
    const float lineHeight = 18.0f * u;
    int count = 1;
    for (const char* c = lines; *c; ++c) count += *c == '\n' ? 1 : 0;
    const float width = std::min(l.width(), 360.0f * u);
    const float height = lineHeight * static_cast<float>(count) + 12.0f * u;
    // On the right, away from the build and the dash button on the left.
    const Rectangle box{l.right - width, l.bottom - height - 90.0f * u, width, height};
    pixelRect(box, theme::kCoopDebugBack);
    char line[96];
    const char* start = lines;
    float y = box.y + 6.0f * u;
    while (*start) {
        const char* end = std::strchr(start, '\n');
        const size_t length = end ? static_cast<size_t>(end - start) : std::strlen(start);
        const size_t copy = std::min(length, sizeof(line) - 1);
        std::memcpy(line, start, copy);
        line[copy] = '\0';
        text(line, box.x + 8.0f * u, y, size, theme::kDebugText);
        y += lineHeight;
        start += length;
        if (*start == '\n') ++start;
    }
    registerExtent("coop debug", box);
}

}  // namespace

void coopHudBeginRun(const char* const names[kMaxPlayers], const int colors[kMaxPlayers]) {
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        std::snprintf(g_names[slot], kNameBytes, "%s", names[slot] ? names[slot] : "");
        g_colors[slot] = colors[slot];
    }
    g_notice[0] = '\0';
    g_noticeLeft = 0.0f;
}

const char* coopHudName(int slot) { return slot >= 0 && slot < kMaxPlayers ? g_names[slot] : ""; }

void coopHudNotice(const char* message) {
    std::snprintf(g_notice, sizeof(g_notice), "%s", message);
    std::snprintf(g_lastNotice, sizeof(g_lastNotice), "%s", message);
    g_noticeLeft = kNoticeSeconds;
}

const char* coopHudLastNotice() { return g_lastNotice; }

int connectionQuality(int pingMs, float snapshotAge) {
    if (snapshotAge > balance::kNetHostSilentSeconds) return 0;
    if (pingMs < 0) return snapshotAge > 0.5f ? 1 : 2;
    if (pingMs < 90 && snapshotAge < 0.25f) return 3;
    if (pingMs < 200 && snapshotAge < 0.5f) return 2;
    return 1;
}

void drawCoopHud(const GameState& state, const CoopHudView& view, float dtReal) {
    const Layout& l = layout();
    const float u = l.u;
    for (int slot = 0; slot < state.playerCount(); ++slot) drawTag(state, slot, u);
    drawSignal(view, u);

    // Under the timer, so it never covers the soldier in the middle of the screen.
    float bannerY = l.top + 70.0f * u;
    if (view.hostAway) {
        drawBanner("Host is away", bannerY, u);
        bannerY += 50.0f * u;
    } else if (view.hostPaused) {
        drawBanner("Paused by the host", bannerY, u);
        bannerY += 50.0f * u;
    }
    if (g_noticeLeft > 0.0f) {
        g_noticeLeft -= dtReal;
        drawBanner(g_notice, bannerY, u);
    }
    if (view.debugText) drawDebug(view.debugText, u);
}

}  // namespace ls::ui
