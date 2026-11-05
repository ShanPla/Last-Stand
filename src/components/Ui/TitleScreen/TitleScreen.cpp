#include "components/Ui/TitleScreen/TitleScreen.h"

#include "components/Maps/Maps.h"
#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Names/Names.h"
#include "components/Ui/Widgets/Widgets.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

Nav g_nav;

// Focus order.
enum Focus { kFocusPlay = 0, kFocusDifficulty, kFocusMap, kFocusSettings, kFocusHowTo, kFocusCount };
constexpr int kSelectorRows = 2;     // Rows of selectors above Settings. The layout budgets for this many.
constexpr float kLogoLeast = 56.0f;  // Smallest logo, in design units, before the best run makes way.
constexpr float kBestPanelWidth = 400.0f;  // The best run's corner panel on wide windows.

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

void drawBest(const BestRun& best, Difficulty level, float x, float y, float width) {
    const float u = ui::u();
    const char* header = "BEST RUN";
    const float headerSize = 16.0f * u;
    text(header, x, y, headerSize, theme::kUiAccent, font::Align::Left, layout().px);
    // Which level these figures are from, so a Hard best is never mistaken for an Easy one.
    text(difficultyName(level), x + font::measure(header, headerSize, layout().px) + 10.0f * u, y, headerSize,
         theme::kDifficultyColors[static_cast<int>(level)], font::Align::Left, layout().px);
    if (best.runs <= 0) {
        char none[48];
        std::snprintf(none, sizeof(none), "No runs on %s yet.", difficultyName(level));
        text(none, x, y + 24.0f * u, 20.0f * u, theme::kUiTextDim);
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

TitleAction drawTitleScreen(SaveData& data) {
    const Layout& l = layout();
    const float u = l.u;
    const bool wide = !l.portrait && l.width() >= 700.0f * u;
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

    // Heights of everything below the logo, so the logo can take what is left. Each
    // selector row (difficulty and map) adds one row to the menu, and the logo and the
    // best run give way, so another row would need no other change.
    const float playHeight = tall(66.0f * u);
    const float smallHeight = tall(50.0f * u);
    const float rowGap = 10.0f * u;
    const float selectorsBlock = (smallHeight + rowGap) * static_cast<float>(kSelectorRows);
    const float fullButtons = playHeight + 12.0f * u + selectorsBlock + smallHeight + rowGap + smallHeight;
    const float bestBlock = 26.0f * u + 76.0f * u;
    const float taglineBlock = 26.0f * u + 26.0f * u;
    // On a very short window, Settings and How to Play share a row, so that every
    // button stays on screen and inside the safe area.
    // Without the side column, the credit line sits under the buttons and needs room too.
    const float creditRoom = wide ? 0.0f : 28.0f * u;
    const float sharedButtons = fullButtons - smallHeight - rowGap;
    const auto bestFits = [&](float buttons) {
        return (l.height() - buttons - taglineBlock - creditRoom - 8.0f * u - bestBlock) / 1.85f >= kLogoLeast * u;
    };
    // A window with no side column has nowhere else for the best run, so it shares the row
    // for that too, when that makes the difference.
    const bool shareForBest = !wide && !bestFits(fullButtons) && bestFits(sharedButtons);
    const bool shareLastRow = shareForBest || l.height() - fullButtons - taglineBlock - creditRoom - 8.0f * u < kLogoLeast * u * 1.85f;
    const float buttonsBlock = shareLastRow ? sharedButtons : fullButtons;
    // The best run is dropped when keeping it would squeeze the logo too small.
    const float roomWithoutBest = l.height() - buttonsBlock - taglineBlock - creditRoom - 8.0f * u;
    const bool roomForBest = bestFits(buttonsBlock);
    const float logoRoom = roomForBest ? roomWithoutBest - bestBlock : roomWithoutBest;
    const float wanted = std::min(wide ? 110.0f * u : 128.0f * u, logoRoom / 1.85f);
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

    // Buttons. The focus order is Play, the selectors, Settings, How to Play.
    const float buttonX = columnX;
    const float buttonWidth = columnWidth;
    float y = taglineY + taglineSize + 26.0f * u;
    // Phones are tall, so the buttons sit lower, where a thumb can reach, but never so low
    // that the last button runs into the credit line.
    if (!wide) y = std::max(y, std::min(l.top + l.height() * 0.42f, l.bottom - creditRoom - buttonsBlock));

    navigate(g_nav, kFocusCount);
    if (button(g_nav, kFocusPlay, Rectangle{buttonX, y, buttonWidth, playHeight}, "PLAY", ButtonLook::Primary, promptConfirm())) action = TitleAction::Play;
    y += playHeight + 12.0f * u;

    int level = static_cast<int>(data.difficulty);
    if (selector(g_nav, kFocusDifficulty, Rectangle{buttonX, y, buttonWidth, smallHeight}, "Difficulty", difficultyNames(), static_cast<int>(Difficulty::Count),
                 &level, theme::kDifficultyColors)) {
        data.difficulty = static_cast<Difficulty>(level);
    }
    y += smallHeight + rowGap;

    static const char* const kMapNames[maps::kMapCount] = {maps::name(maps::MapId::City), maps::name(maps::MapId::Wasteland)};
    int map = static_cast<int>(maps::mapFromIndex(data.map));
    if (selector(g_nav, kFocusMap, Rectangle{buttonX, y, buttonWidth, smallHeight}, "Map", kMapNames, maps::kMapCount, &map, theme::kMapColors)) {
        data.map = map;
    }
    y += smallHeight + rowGap;

    if (shareLastRow) {
        const float halfWidth = (buttonWidth - rowGap) * 0.5f;
        if (button(g_nav, kFocusSettings, Rectangle{buttonX, y, halfWidth, smallHeight}, "Settings")) action = TitleAction::Settings;
        if (button(g_nav, kFocusHowTo, Rectangle{buttonX + halfWidth + rowGap, y, halfWidth, smallHeight}, "How to Play")) action = TitleAction::HowToPlay;
    } else {
        if (button(g_nav, kFocusSettings, Rectangle{buttonX, y, buttonWidth, smallHeight}, "Settings")) action = TitleAction::Settings;
        y += smallHeight + rowGap;
        if (button(g_nav, kFocusHowTo, Rectangle{buttonX, y, buttonWidth, smallHeight}, "How to Play")) action = TitleAction::HowToPlay;
    }
    y += smallHeight + 26.0f * u;

    // Best run for the chosen level, under the buttons if there is room. A wide window has no
    // room in the column, but it has the whole demo side, so the record goes in a corner there.
    const float bestHeight = 24.0f * u + 34.0f * u + 22.0f * u;
    if (roomForBest && y + 76.0f * u < l.bottom) {
        drawBest(data.bestFor(data.difficulty), data.difficulty, buttonX, y, buttonWidth);
        registerExtent("best run", Rectangle{buttonX, y, buttonWidth, bestHeight});
    } else if (wide) {
        const float pad = 14.0f * u;
        const float width = std::min(kBestPanelWidth * u, l.right - (columnX + columnWidth + 40.0f * u) - 12.0f * u);
        const Rectangle plate{l.right - width, l.bottom - 30.0f * u - bestHeight - pad * 2.0f, width, bestHeight + pad * 2.0f};
        panel(plate, withAlpha(theme::kHudPlate, 0.92f), theme::kUiBorder, 0.0f);
        drawBest(data.bestFor(data.difficulty), data.difficulty, plate.x + pad, plate.y + pad, width - pad * 2.0f);
        registerExtent("best run", plate);
    }

    // Credit line in the corner.
    const char* credit = "C++20  -  raylib  -  WebAssembly";
    if (wide) text(credit, l.right, l.bottom - 20.0f * u, 16.0f * u, theme::kUiTextDim, font::Align::Right);
    else text(credit, l.centerX(), l.bottom - 20.0f * u, 16.0f * u, theme::kUiTextDim, font::Align::Center);
    return action;
}

}  // namespace ls::ui
