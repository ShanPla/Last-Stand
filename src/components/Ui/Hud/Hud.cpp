#include "components/Ui/Hud/Hud.h"

#include "data/Theme.h"

#include "raylib.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

void drawHud(const GameState& state) {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    const float unit = std::max(1.0f, std::min(width, height) / 720.0f);
    const float margin = 16.0f * unit;
    const Player& player = state.player;

    // XP bar across the top edge.
    const float xpHeight = 8.0f * unit;
    const float xpFraction = static_cast<float>(player.xp) / static_cast<float>(std::max(1, player.xpToNext));
    DrawRectangleRec(Rectangle{0.0f, 0.0f, width, xpHeight}, theme::kBarBack);
    DrawRectangleRec(Rectangle{0.0f, 0.0f, width * xpFraction, xpHeight}, theme::kXpFill);

    // Health bar at the top left.
    const float barWidth = std::min(width * 0.4f, 280.0f * unit);
    const float barHeight = 16.0f * unit;
    const float barY = xpHeight + margin * 0.75f;
    const float hpFraction = std::max(0.0f, player.hp / player.maxHp);
    DrawRectangleRec(Rectangle{margin, barY, barWidth, barHeight}, theme::kBarBack);
    DrawRectangleRec(Rectangle{margin, barY, barWidth * hpFraction, barHeight}, theme::kHealthFill);

    char text[64];
    const int fontSize = static_cast<int>(22.0f * unit);
    std::snprintf(text, sizeof(text), "LV %d", player.level);
    DrawText(text, static_cast<int>(margin), static_cast<int>(barY + barHeight + 6.0f * unit), fontSize, theme::kText);

    // Timer at the top center, kills at the top right.
    const int seconds = static_cast<int>(state.timeSeconds());
    std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    const int timerWidth = MeasureText(text, fontSize * 3 / 2);
    DrawText(text, static_cast<int>((width - static_cast<float>(timerWidth)) * 0.5f), static_cast<int>(barY), fontSize * 3 / 2, theme::kText);

    std::snprintf(text, sizeof(text), "%d KILLS", state.run.kills);
    const int killsWidth = MeasureText(text, fontSize);
    DrawText(text, static_cast<int>(width - margin - static_cast<float>(killsWidth)), static_cast<int>(barY), fontSize, theme::kTextDim);
}

}  // namespace ls::ui
