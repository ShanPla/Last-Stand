#include "components/Ui/GameOverScreen/GameOverScreen.h"

#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Names/Names.h"
#include "components/Ui/Widgets/Widgets.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

constexpr float kInputDelaySeconds = 0.8f;  // Long enough to stop a mid-fight press, short enough not to annoy.
constexpr float kFadeStart = 0.30f;         // The world is shown for a moment before the screen fades in.
constexpr float kFadeSeconds = 0.40f;
constexpr int kBarSlots = static_cast<int>(WeaponId::Count);

Nav g_nav;

float smoothstep(float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

void formatDamage(char* out, size_t size, float value) {
    if (value >= 10000.0f) std::snprintf(out, size, "%.1fk", static_cast<double>(value) / 1000.0);
    else std::snprintf(out, size, "%d", static_cast<int>(value + 0.5f));
}

void drawNewBest(float cx, float y) {
    const float u = ui::u();
    const float size = 14.0f * u;
    const float width = font::measure("NEW BEST", size, 1.2f * u) + 16.0f * u;
    const Rectangle chip{cx - width * 0.5f, y, width, 20.0f * u};
    roundedRect(chip, 10.0f * u, theme::kNewBest);
    textMiddle("NEW BEST", cx, y + 10.0f * u, size, theme::kTextOnPrimary, font::Align::Center, 1.2f * u);
}

void drawStat(float cx, float y, const char* value, const char* label, bool best) {
    const float u = ui::u();
    textShadowed(value, cx, y, 42.0f * u, theme::kUiText, font::Align::Center);
    text(label, cx, y + 44.0f * u, 14.0f * u, theme::kUiTextDim, font::Align::Center, 2.0f * u);
    if (best) drawNewBest(cx, y + 64.0f * u);
}

}  // namespace

void resetGameOverScreen() { g_nav = Nav{}; }

GameOverAction drawGameOverScreen(const RunSummary& summary, float age) {
    const Layout& l = layout();
    const float u = l.u;
    GameOverAction action = GameOverAction::None;
    const float fade = smoothstep((age - kFadeStart) / kFadeSeconds);
    setFade(fade);
    const bool ready = age >= kInputDelaySeconds;

    DrawRectangle(0, 0, static_cast<int>(l.w), static_cast<int>(l.h), tint(theme::kScreenScrim));
    const Color accent = summary.won ? theme::kVictory : theme::kDefeat;

    // Sort weapons by damage, highest first. Ten items, so a simple insertion sort is plenty.
    int order[kBarSlots];
    int shown = 0;
    for (int i = 0; i < kBarSlots; ++i) {
        if (summary.damageByWeapon[i] <= 0.0f) continue;
        int j = shown++;
        while (j > 0 && summary.damageByWeapon[order[j - 1]] < summary.damageByWeapon[i]) {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = i;
    }

    // Panel size: as tall as its content needs, centered.
    const float panelWidth = std::min(l.width(), 760.0f * u);
    const float x = l.centerX() - panelWidth * 0.5f;
    const float pad = 22.0f * u;
    const float headlineSize = (l.portrait ? 54.0f : 66.0f) * u;
    const float headerHeight = pad + headlineSize + 28.0f * u;
    const float statsHeight = 92.0f * u;
    const float barRow = 34.0f * u;
    const float buttonsHeight = tall(58.0f * u);
    const float fixed = headerHeight + statsHeight + 28.0f * u + buttonsHeight + pad * 1.2f;
    const int maxBars = std::max(1, std::min(6, static_cast<int>((l.height() - fixed) / barRow)));
    const int bars = std::min(shown, maxBars);
    const float barsHeight = bars > 0 ? 22.0f * u + static_cast<float>(bars) * barRow : 0.0f;
    const float panelHeight = std::min(l.height(), fixed + barsHeight);
    const Rectangle panelRect{x, l.top + (l.height() - panelHeight) * 0.5f, panelWidth, panelHeight};
    panel(panelRect, theme::kUiPanel, withAlpha(accent, 0.75f), 20.0f * u);

    float y = panelRect.y + pad * 0.7f;
    textShadowed(summary.won ? "VICTORY" : "YOU DIED", l.centerX(), y, headlineSize, accent, font::Align::Center, 6.0f * u);
    y += headlineSize + 2.0f * u;
    text(summary.won ? "The boss is down. You survived." : "The horde got you.", l.centerX(), y, 20.0f * u, theme::kUiTextDim, font::Align::Center);
    y += 34.0f * u;

    // The three headline numbers.
    char value[24];
    const float columnWidth = (panelWidth - pad * 2.0f) / 3.0f;
    const float firstX = x + pad + columnWidth * 0.5f;
    const int seconds = static_cast<int>(summary.timeSeconds);
    std::snprintf(value, sizeof(value), "%d:%02d", seconds / 60, seconds % 60);
    drawStat(firstX, y, value, "TIME SURVIVED", summary.newBestTime);
    std::snprintf(value, sizeof(value), "%d", summary.kills);
    drawStat(firstX + columnWidth, y, value, "KILLS", summary.newBestKills);
    std::snprintf(value, sizeof(value), "%d", summary.level);
    drawStat(firstX + columnWidth * 2.0f, y, value, "LEVEL", summary.newBestLevel);
    y += statsHeight + 6.0f * u;

    // Damage per weapon, as bars.
    if (bars > 0) {
        text("DAMAGE BY WEAPON", x + pad, y, 14.0f * u, theme::kUiTextDim, font::Align::Left, 2.0f * u);
        y += 22.0f * u;
        const float topDamage = summary.damageByWeapon[order[0]];
        const float nameWidth = std::min(190.0f * u, panelWidth * 0.3f);
        const float valueWidth = 70.0f * u;
        const float barX = x + pad + 36.0f * u + nameWidth;
        const float barWidth = x + panelWidth - pad - valueWidth - barX;
        for (int i = 0; i < bars; ++i) {
            const int weapon = order[i];
            const float cy = y + barRow * 0.5f;
            const Color color = theme::kBarPalette[i % 6];
            drawWeaponIcon(static_cast<WeaponId>(weapon), x + pad + 14.0f * u, cy, 26.0f * u, tint(color));
            const char* name = weaponName(static_cast<WeaponId>(weapon));
            textMiddle(name, x + pad + 36.0f * u, cy, font::fitSize(name, 18.0f * u, nameWidth - 8.0f * u), theme::kUiText);
            bar(Rectangle{barX, cy - 7.0f * u, barWidth, 14.0f * u}, summary.damageByWeapon[weapon] / topDamage, color, theme::kUiBarBack);
            formatDamage(value, sizeof(value), summary.damageByWeapon[weapon]);
            textMiddle(value, x + panelWidth - pad, cy, 18.0f * u, theme::kUiText, font::Align::Right);
            y += barRow;
        }
    }

    // Buttons. They look dim until the input delay is over.
    const float buttonY = panelRect.y + panelRect.height - pad * 0.7f - buttonsHeight;
    const float restartWidth = std::min(300.0f * u, (panelWidth - pad * 2.0f) * 0.6f);
    const float quitWidth = std::min(200.0f * u, (panelWidth - pad * 2.0f) * 0.4f - 12.0f * u);
    const float buttonsLeft = l.centerX() - (restartWidth + 12.0f * u + quitWidth) * 0.5f;
    navigate(g_nav, 2, true);
    const bool wasEnabled = inputEnabled();
    setInputEnabled(ready);
    if (button(g_nav, 0, Rectangle{buttonsLeft, buttonY, restartWidth, buttonsHeight}, "RESTART", ButtonLook::Primary, promptConfirm(), ready)) {
        action = GameOverAction::Restart;
    }
    if (button(g_nav, 1, Rectangle{buttonsLeft + restartWidth + 12.0f * u, buttonY, quitWidth, buttonsHeight}, "Quit", ButtonLook::Normal, nullptr, ready)) {
        action = GameOverAction::Quit;
    }
    setInputEnabled(wasEnabled);
    return action;
}

}  // namespace ls::ui
