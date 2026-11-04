#include "components/Ui/TitleScreen/TitleScreen.h"

#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Widgets/Widgets.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

Nav g_nav;

// Chunky logo lettering: a stack of hard dark-red copies behind the letters, so the
// word looks cut out of a block, like the title of an old arcade game.
void logoText(const char* string, float x, float y, float size, Color color, font::Align align, float tracking) {
    const float p = layout().px;
    for (int i = 4; i >= 1; --i) text(string, x + p * static_cast<float>(i), y + p * static_cast<float>(i), size, theme::kLogoShadow, align, tracking);
    text(string, x, y, size, color, align, tracking);
}

void formatTime(char* out, size_t size, float seconds) {
    const int total = static_cast<int>(seconds);
    std::snprintf(out, size, "%d:%02d", total / 60, total % 60);
}

// One best-run figure: a big value over a small label.
void drawStat(float x, float y, float width, const char* value, const char* label) {
    const float u = ui::u();
    text(value, x + width * 0.5f, y, 32.0f * u, theme::kUiText, font::Align::Center);
    text(label, x + width * 0.5f, y + 34.0f * u, 16.0f * u, theme::kUiTextDim, font::Align::Center, layout().px);
}

void drawBest(const BestRun& best, float x, float y, float width) {
    const float u = ui::u();
    const char* header = "BEST RUN";
    text(header, x, y, 16.0f * u, theme::kUiAccent, font::Align::Left, layout().px);
    if (best.runs <= 0) {
        text("No runs yet. Your best is saved here.", x, y + 24.0f * u, 20.0f * u, theme::kUiTextDim);
        return;
    }
    char timeText[16];
    char killsText[16];
    char levelText[16];
    char winsText[16];
    formatTime(timeText, sizeof(timeText), best.bestTimeSeconds);
    std::snprintf(killsText, sizeof(killsText), "%d", best.mostKills);
    std::snprintf(levelText, sizeof(levelText), "%d", best.highestLevel);
    std::snprintf(winsText, sizeof(winsText), "%d", best.wins);
    const float cell = width / 4.0f;
    const float top = y + 24.0f * u;
    drawStat(x, top, cell, timeText, "TIME");
    drawStat(x + cell, top, cell, killsText, "KILLS");
    drawStat(x + cell * 2.0f, top, cell, levelText, "LEVEL");
    drawStat(x + cell * 3.0f, top, cell, winsText, "WINS");
}

}  // namespace

void resetTitleScreen() { g_nav = Nav{}; }

TitleAction drawTitleScreen(const BestRun& best) {
    const Layout& l = layout();
    const float u = l.u;
    const bool wide = !l.portrait && l.width() >= 760.0f * u;
    TitleAction action = TitleAction::None;

    // The column the menu sits in: left aligned on wide screens, centered otherwise.
    const float columnWidth = wide ? std::min(430.0f * u, l.width() * 0.5f) : std::min(l.width(), 440.0f * u);
    const float columnX = wide ? l.left + 24.0f * u : l.centerX() - columnWidth * 0.5f;
    const font::Align align = wide ? font::Align::Left : font::Align::Center;
    const float textX = wide ? columnX : l.centerX();

    // A flat dark shade so the text reads, with a hard edge. On wide screens it only
    // covers the menu side, so the demo stays visible on the other.
    if (wide) {
        const float edge = snapPx(columnX + columnWidth + 40.0f * u);
        pixelRect(Rectangle{0.0f, 0.0f, edge, l.h}, withAlpha(theme::kTitleShade, 0.9f));
        pixelRect(Rectangle{edge, 0.0f, l.px, l.h}, theme::kUiBorderDark);
    } else {
        pixelRect(Rectangle{0.0f, 0.0f, l.w, l.h}, withAlpha(theme::kTitleShade, 0.72f));
    }

    // Heights of everything below the logo, so the logo can take what is left.
    const float playHeight = tall(66.0f * u);
    const float smallHeight = tall(50.0f * u);
    const float buttonsBlock = playHeight + 12.0f * u + smallHeight + 10.0f * u + smallHeight;
    const float bestBlock = 26.0f * u + 76.0f * u;
    const float taglineBlock = 26.0f * u + 26.0f * u;
    const float logoRoom = l.height() - buttonsBlock - bestBlock - taglineBlock - 8.0f * u;
    const float wanted = wide ? std::min(110.0f * u, logoRoom / 1.85f) : 128.0f * u;
    const float logoSize = font::fitSize("STAND", font::snapSize(wanted * 0.95f), columnWidth - 5.0f * l.px, l.px);

    const float logoTop = wide ? l.top + 6.0f * u : l.top + 10.0f * u;
    const float lastWidth = font::measure("LAST", logoSize, l.px);
    logoText("LAST", textX, logoTop, logoSize, theme::kUiText, align, l.px);
    logoText("STAND", textX, logoTop + logoSize * 0.82f, logoSize, theme::kLogoRust, align, l.px);
    // The zombie sits beside the first word.
    const float zombieSize = logoSize * 0.78f;
    const float zombieX = wide ? textX + lastWidth + 14.0f * u + zombieSize * 0.5f : textX + lastWidth * 0.5f + 14.0f * u + zombieSize * 0.5f;
    drawZombieIcon(zombieX, logoTop + logoSize * 0.46f, zombieSize, fade());

    const char* tagline = "Survive ten minutes. Then face what is waiting.";
    const float taglineY = logoTop + logoSize * 1.82f;
    const float taglineSize = font::fitSize(tagline, 22.0f * u, columnWidth);
    textShadowed(tagline, textX, taglineY, taglineSize, theme::kUiTextDim, align);

    // Buttons.
    const float buttonX = columnX;
    const float buttonWidth = columnWidth;
    float y = taglineY + taglineSize + 26.0f * u;
    // Phones are tall, so the buttons sit lower, where a thumb can reach.
    if (!wide) y = std::max(y, l.top + l.height() * 0.42f);

    navigate(g_nav, 3);
    if (button(g_nav, 0, Rectangle{buttonX, y, buttonWidth, playHeight}, "PLAY", ButtonLook::Primary, promptConfirm())) action = TitleAction::Play;
    y += playHeight + 12.0f * u;
    if (button(g_nav, 1, Rectangle{buttonX, y, buttonWidth, smallHeight}, "Settings")) action = TitleAction::Settings;
    y += smallHeight + 10.0f * u;
    if (button(g_nav, 2, Rectangle{buttonX, y, buttonWidth, smallHeight}, "How to Play")) action = TitleAction::HowToPlay;
    y += smallHeight + 26.0f * u;

    // Best run, as long as there is room under the buttons.
    if (y + 76.0f * u < l.bottom) drawBest(best, buttonX, y, buttonWidth);

    // Credit line in the corner.
    const char* credit = "C++20  -  raylib  -  WebAssembly";
    if (wide) text(credit, l.right, l.bottom - 20.0f * u, 16.0f * u, theme::kUiTextDim, font::Align::Right);
    else text(credit, l.centerX(), l.bottom - 20.0f * u, 16.0f * u, theme::kUiTextDim, font::Align::Center);
    return action;
}

}  // namespace ls::ui
