#include "components/Ui/LevelUpCards/LevelUpCards.h"

#include "data/Balance.h"
#include "data/Theme.h"

#include "raylib.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

constexpr float kInputDelaySeconds = 0.35f;  // Stops a held key or fast tap choosing by accident.

float g_openTime = 0.0f;
int g_selected = 0;

const char* const kWeaponNames[] = {"Pulse", "Orbit", "Nova", "Arc", "Mines", "Lance", "Swarm", "Ripple"};
const char* const kPassiveNames[] = {"Damage", "Fire Rate", "Area", "Move Speed", "Pickup Range", "Armor"};

const char* cardTitle(const Card& card) {
    switch (card.kind) {
        case CardKind::Weapon: return kWeaponNames[static_cast<int>(card.weapon)];
        case CardKind::Passive: return kPassiveNames[static_cast<int>(card.passive)];
        case CardKind::Evolution:
            for (const balance::Evolution& evolution : balance::kEvolutions) {
                if (evolution.weapon == card.weapon) return evolution.name;
            }
            return "Evolution";
        case CardKind::Heal: return "Heal";
    }
    return "";
}

Rectangle cardRect(int index, int count) {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    const float gap = std::min(width, height) * 0.03f;
    if (width >= height) {
        const float cardWidth = std::min((width - gap * 4.0f) / 3.0f, height * 0.55f);
        const float cardHeight = height * 0.55f;
        const float total = cardWidth * static_cast<float>(count) + gap * static_cast<float>(count - 1);
        const float startX = (width - total) * 0.5f;
        return Rectangle{startX + static_cast<float>(index) * (cardWidth + gap), height * 0.26f, cardWidth, cardHeight};
    }
    const float cardWidth = width - gap * 2.0f;
    const float cardHeight = height * 0.2f;
    const float total = cardHeight * static_cast<float>(count) + gap * static_cast<float>(count - 1);
    const float startY = (height - total) * 0.5f;
    return Rectangle{gap, startY + static_cast<float>(index) * (cardHeight + gap), cardWidth, cardHeight};
}

}  // namespace

LevelUpChoice updateLevelUpCards(const GameState& state, const input::Snapshot& snapshot, float dtReal) {
    LevelUpChoice choice;
    g_openTime += dtReal;
    if (g_openTime < kInputDelaySeconds) return choice;

    const int count = state.run.offerCount;
    if (snapshot.left || snapshot.up) g_selected = (g_selected + count - 1) % count;
    if (snapshot.right || snapshot.down) g_selected = (g_selected + 1) % count;
    if (snapshot.confirm) choice.card = g_selected;
    if (snapshot.reroll) choice.reroll = true;
    if (snapshot.pointerPressed) {
        for (int i = 0; i < count; ++i) {
            if (CheckCollisionPointRec(Vector2{snapshot.pointer.x, snapshot.pointer.y}, cardRect(i, count))) {
                choice.card = i;
            }
        }
    }
    if (choice.card >= 0) resetLevelUpCards();
    if (choice.reroll) g_openTime = 0.0f;
    return choice;
}

void resetLevelUpCards() {
    g_openTime = 0.0f;
    g_selected = 0;
}

void drawLevelUpCards(const GameState& state) {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    const float unit = std::max(1.0f, std::min(width, height) / 720.0f);
    DrawRectangleRec(Rectangle{0.0f, 0.0f, width, height}, theme::kScrim);

    const int titleSize = static_cast<int>(44.0f * unit);
    const char* title = "LEVEL UP";
    DrawText(title, static_cast<int>((width - static_cast<float>(MeasureText(title, titleSize))) * 0.5f),
             static_cast<int>(height * 0.1f), titleSize, theme::kAccent);

    char text[64];
    for (int i = 0; i < state.run.offerCount; ++i) {
        const Card& card = state.run.offers[i];
        const Rectangle rect = cardRect(i, state.run.offerCount);
        DrawRectangleRec(rect, theme::kPanel);
        DrawRectangleLinesEx(rect, (i == g_selected ? 4.0f : 2.0f) * unit, i == g_selected ? theme::kAccent : theme::kPanelEdge);
        const int nameSize = static_cast<int>(30.0f * unit);
        const float textX = rect.x + 16.0f * unit;
        DrawText(cardTitle(card), static_cast<int>(textX), static_cast<int>(rect.y + 18.0f * unit), nameSize, theme::kText);
        std::snprintf(text, sizeof(text), "Level %d", card.newLevel);
        DrawText(text, static_cast<int>(textX), static_cast<int>(rect.y + 58.0f * unit), static_cast<int>(22.0f * unit), theme::kTextDim);
    }

    std::snprintf(text, sizeof(text), "Rerolls left: %d", state.run.rerollsLeft);
    const int hintSize = static_cast<int>(22.0f * unit);
    DrawText(text, static_cast<int>((width - static_cast<float>(MeasureText(text, hintSize))) * 0.5f),
             static_cast<int>(height * 0.9f), hintSize, theme::kTextDim);
}

}  // namespace ls::ui
