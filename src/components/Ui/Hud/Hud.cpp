#include "components/Ui/Hud/Hud.h"

#include "components/Ui/BuildStrip/BuildStrip.h"
#include "components/Ui/Widgets/Widgets.h"
#include "core/Save.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ls::ui {

namespace {

constexpr float kLowHealthFraction = 0.3f;     // Below this, the edge warning shows.
constexpr float kPulseHertz = 1.1f;            // Slow enough to be calm, and far under flash limits.
constexpr float kPauseSize = 56.0f;            // In design units.
constexpr float kPauseTouchPadding = 10.0f;    // Extra hit area around the drawn button.
constexpr const char* kBossName = "PATIENT ZERO";
constexpr int kVignetteBands = 4;

// A warm warning along the four screen edges, never in the middle. It is drawn as a
// few hard-edged bands, each fainter than the one outside it, so it stays pixel-crisp.
void drawLowHealthEdge(const Layout& l, float danger) {
    const Settings& settings = save::current().settings;
    const float t = uiSeconds();
    const bool still = settings.reducedMotion || settings.flashIntensity < 0.05f;
    const float pulse = still ? 0.5f : 0.5f + 0.5f * std::sin(t * kTau * kPulseHertz);
    // Flash intensity scales how far the pulse swings. Static, it stays a fixed gentle band.
    const float swing = still ? 0.0f : settings.flashIntensity;
    const float strength = danger * (0.34f + 0.34f * swing * pulse);
    const float band = snapPx(std::min(l.w, l.h) * 0.14f / static_cast<float>(kVignetteBands));
    for (int i = 0; i < kVignetteBands; ++i) {
        const float fade = 1.0f - static_cast<float>(i) / static_cast<float>(kVignetteBands);
        const Color c = withAlpha(theme::kLowHealth, strength * 0.6f * fade * fade);
        const float in = band * static_cast<float>(i);
        pixelRect(Rectangle{in, in, l.w - in * 2.0f, band}, c);
        pixelRect(Rectangle{in, l.h - in - band, l.w - in * 2.0f, band}, c);
        pixelRect(Rectangle{in, in + band, band, l.h - in * 2.0f - band * 2.0f}, c);
        pixelRect(Rectangle{l.w - in - band, in + band, band, l.h - in * 2.0f - band * 2.0f}, c);
    }
}

// Two square bars, like the pause symbol on any console.
void drawPauseIcon(Rectangle r, bool hot) {
    const float p = layout().px;
    const float barWidth = snapPx(r.width * 0.14f);
    const float barHeight = snapPx(r.height * 0.4f);
    const Color color = hot ? theme::kUiAccent : theme::kUiText;
    const float top = r.y + (r.height - barHeight) * 0.5f;
    const float gap = std::max(p, barWidth);
    pixelRect(Rectangle{r.x + r.width * 0.5f - gap * 0.5f - barWidth, top, barWidth, barHeight}, color);
    pixelRect(Rectangle{r.x + r.width * 0.5f + gap * 0.5f, top, barWidth, barHeight}, color);
}

const char* moveHint() {
    switch (input().lastDevice) {
        case input::Device::Touch: return "Touch and drag anywhere to move";
        case input::Device::Gamepad: return "Left stick to move";
        default: return "WASD or arrow keys to move";
    }
}

void drawControlsHint(const Layout& l, float alpha) {
    if (alpha <= 0.01f) return;
    const float size = 22.0f * l.u;
    const char* first = moveHint();
    const char* second = "Weapons fire on their own. Collect green gems.";
    const float width = std::max(font::measure(first, size), font::measure(second, size * 0.72f)) + 36.0f * l.u;
    const float height = size * 2.5f;
    const float maxWidth = l.width();
    const float w = std::min(width, maxWidth);
    const Rectangle plate{l.centerX() - w * 0.5f, l.bottom - height - 20.0f * l.u, w, height};
    panel(plate, withAlpha(theme::kHudPlate, alpha * 1.4f), withAlpha(theme::kUiBorder, alpha), 0.0f);
    const float fitted = font::fitSize(first, size, w - 24.0f * l.u);
    text(first, plate.x + w * 0.5f, plate.y + height * 0.1f, fitted, withAlpha(theme::kUiText, alpha), font::Align::Center);
    const float small = font::fitSize(second, size * 0.72f, w - 24.0f * l.u);
    text(second, plate.x + w * 0.5f, plate.y + height * 0.56f, small, withAlpha(theme::kUiTextDim, alpha), font::Align::Center);
}

void drawBossBar(const Layout& l, const RunState& run) {
    const float width = std::min(560.0f * l.u, l.width() * 0.8f);
    const float height = 14.0f * l.u;
    const float x = l.centerX() - width * 0.5f;
    const float y = l.bottom - height - 4.0f * l.u;
    textShadowed(kBossName, l.centerX(), y - 26.0f * l.u, 20.0f * l.u, theme::kBossBar, font::Align::Center, 3.0f * l.u);
    bar(Rectangle{x, y, width, height}, run.bossHpFraction, theme::kBossBar, theme::kBossBarBack);
    // Notches where the boss changes its attacks.
    const float notches[2] = {balance::kBossPhase2HpFraction, balance::kBossPhase3HpFraction};
    for (float n : notches) {
        pixelRect(Rectangle{x + width * n - layout().px, y, layout().px * 2.0f, height}, theme::kUiBorderDark);
    }
}

Rectangle pauseRect(const Layout& l) {
    const float size = kPauseSize * l.u;
    return Rectangle{l.right - size, l.top + 8.0f * l.u, size, size};
}

}  // namespace

input::Rect pauseButtonRect() {
    const Layout& l = layout();
    const Rectangle r = pauseRect(l);
    const float pad = kPauseTouchPadding * l.u;
    return input::Rect{r.x - pad, r.y - pad, r.width + pad * 2.0f, r.height + pad * 2.0f};
}

bool drawHud(const GameState& state, float hintAlpha, bool pauseEnabled) {
    const Layout& l = layout();
    const float u = l.u;
    const Player& player = state.player;
    const RunState& run = state.run;

    const float hpFraction = player.maxHp > 0.0f ? std::max(0.0f, player.hp / player.maxHp) : 0.0f;
    if (hpFraction < kLowHealthFraction && player.hp > 0.0f) {
        drawLowHealthEdge(l, 1.0f - hpFraction / kLowHealthFraction * 0.5f);
    }

    // XP bar along the top edge of the safe area.
    const float xpHeight = 6.0f * u;
    const float xpFraction = static_cast<float>(player.xp) / static_cast<float>(std::max(1, player.xpToNext));
    bar(Rectangle{l.left, l.top, l.width(), xpHeight}, xpFraction, theme::kUiXp, withAlpha(theme::kUiBarBack, 0.85f));

    char buffer[64];
    const float rowY = l.top + xpHeight + 10.0f * u;

    // Health bar, top left.
    const float healthWidth = std::min(210.0f * u, l.width() * 0.4f);
    const float healthHeight = 22.0f * u;
    const Rectangle healthRect{l.left, rowY, healthWidth, healthHeight};
    bar(healthRect, hpFraction, theme::kUiHealth, withAlpha(theme::kUiBarBack, 0.9f));
    std::snprintf(buffer, sizeof(buffer), "%d / %d", static_cast<int>(std::ceil(std::max(0.0f, player.hp))), static_cast<int>(player.maxHp));
    textMiddle(buffer, healthRect.x + 10.0f * u, rowY + healthHeight * 0.5f, 16.0f * u, theme::kUiText, font::Align::Left);

    std::snprintf(buffer, sizeof(buffer), "LV %d", player.level);
    textShadowed(buffer, l.left + 2.0f * u, rowY + healthHeight + 4.0f * u, 22.0f * u, theme::kUiXp);

    // The held build below.
    const float weaponSlot = 46.0f * u;
    const float weaponY = rowY + healthHeight + 34.0f * u;
    drawWeaponSlots(state, l.left, weaponY, weaponSlot);
    const float passiveSlot = 32.0f * u;
    drawPassiveSlots(state, l.left, weaponY + weaponSlotsHeight(weaponSlot) + 6.0f * u, passiveSlot);

    // Timer, top center.
    const int seconds = static_cast<int>(state.timeSeconds());
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
    textShadowed(buffer, l.centerX(), rowY - 6.0f * u, 34.0f * u, theme::kUiText, font::Align::Center);

    // Pause button and kills, top right.
    const Rectangle pause = pauseRect(l);
    const bool hot = input().lastDevice == input::Device::Mouse && inside(input().mouse, pause);
    panel(pause, hot ? theme::kButtonHover : theme::kButtonFill, hot ? theme::kUiAccent : theme::kUiBorder, 0.0f);
    drawPauseIcon(pause, hot);
    registerRect("Pause", pause);
    std::snprintf(buffer, sizeof(buffer), "%d", run.kills);
    textShadowed(buffer, pause.x - 12.0f * u, rowY - 2.0f * u, 28.0f * u, theme::kUiText, font::Align::Right);
    textShadowed("KILLS", pause.x - 12.0f * u, rowY + 26.0f * u, 15.0f * u, theme::kUiTextDim, font::Align::Right, 1.5f * u);

    if (run.bossSpawned && run.bossHpFraction > 0.0f) drawBossBar(l, run);
    drawControlsHint(l, hintAlpha);

    const input::Rect hit = pauseButtonRect();
    const bool pressed = pauseEnabled && inputEnabled() && input().pointerPressed &&
                         inside(input().pointer, Rectangle{hit.x, hit.y, hit.w, hit.h});
    return pressed;
}

}  // namespace ls::ui
