#include "components/Ui/SettingsScreen/SettingsScreen.h"

#include "components/Ui/Widgets/Widgets.h"
#include "data/Theme.h"

#include <algorithm>

namespace ls::ui {

namespace {

constexpr int kRowCount = 6;
constexpr float kStep = 0.1f;  // Keyboard and gamepad change sliders in tenths.

Nav g_nav;

}  // namespace

void resetSettingsScreen() { g_nav = Nav{}; }

SettingsResult drawSettingsScreen(Settings& settings, const char* backLabel) {
    const Layout& l = layout();
    const float u = l.u;
    SettingsResult result;

    pixelRect(Rectangle{0.0f, 0.0f, l.w, l.h}, theme::kScreenScrim);

    // Rows share the height that is left. A short wide window, such as a phone on its
    // side, splits them into two columns so each row can stay finger-sized.
    const float titleHeight = 62.0f * u;
    const float backHeight = tall(54.0f * u);
    const float gap = 9.0f * u;
    const float minRow = std::max(44.0f * u, l.touch);
    const float availableSingle = l.height() - titleHeight - backHeight - gap * static_cast<float>(kRowCount + 1);
    const bool twoColumns = !l.portrait && l.width() >= 760.0f * u && availableSingle / static_cast<float>(kRowCount) < minRow;
    const int columns = twoColumns ? 2 : 1;
    const int rowsPerColumn = kRowCount / columns;

    const float panelWidth = std::min(l.width(), (twoColumns ? 980.0f : 640.0f) * u);
    const float x = l.centerX() - panelWidth * 0.5f;
    const float columnWidth = (panelWidth - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float available = l.height() - titleHeight - backHeight - gap * static_cast<float>(rowsPerColumn + 1);
    const float rowHeight = std::max(minRow, std::min(58.0f * u, available / static_cast<float>(rowsPerColumn)));
    const float contentHeight = titleHeight + rowHeight * static_cast<float>(rowsPerColumn) + gap * static_cast<float>(rowsPerColumn) + backHeight;
    float y = l.top + std::max(0.0f, (l.height() - contentHeight) * 0.5f);

    textShadowed("SETTINGS", l.centerX(), y, 44.0f * u, theme::kUiAccent, font::Align::Center, layout().px);
    y += titleHeight;

    navigate(g_nav, kRowCount + 1);
    const auto row = [&](int index) {
        const int column = index / rowsPerColumn;
        const int inColumn = index % rowsPerColumn;
        return Rectangle{x + static_cast<float>(column) * (columnWidth + gap), y + static_cast<float>(inColumn) * (rowHeight + gap), columnWidth, rowHeight};
    };

    if (slider(g_nav, 0, row(0), "Music volume", &settings.musicVolume, kStep)) result.changed = true;
    if (slider(g_nav, 1, row(1), "Effects volume", &settings.effectsVolume, kStep)) result.changed = true;
    if (slider(g_nav, 2, row(2), "Screen shake", &settings.screenShake, kStep)) result.changed = true;
    if (slider(g_nav, 3, row(3), "Flash intensity", &settings.flashIntensity, kStep)) result.changed = true;
    if (toggle(g_nav, 4, row(4), "Damage numbers", &settings.damageNumbers)) result.changed = true;
    if (toggle(g_nav, 5, row(5), "Reduced motion", &settings.reducedMotion)) result.changed = true;

    const float backWidth = twoColumns ? 360.0f * u : panelWidth;
    const Rectangle back{l.centerX() - backWidth * 0.5f, y + static_cast<float>(rowsPerColumn) * (rowHeight + gap) + 2.0f * u, backWidth, backHeight};
    if (button(g_nav, kRowCount, back, backLabel, ButtonLook::Primary, promptBack())) result.back = true;
    return result;
}

}  // namespace ls::ui
