#include "components/Ui/HowToPlay/HowToPlay.h"

#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Names/Names.h"
#include "components/Ui/Widgets/Widgets.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

Nav g_nav;

enum class Glyph { Move, Fire, Gem, Cards, Ammo, Evolve, Pickups, Goal };

constexpr int kTipCount = 8;
constexpr int kMaxBodyLines = 4;

struct Tip {
    Glyph glyph;
    const char* title;
    const char* body;
};

// One way of laying the tips out. The first plan that fits the window is used, so a roomy
// window gets the big airy one and a squeezed window (a 4:3 screen, a square one) gets a
// tighter one instead of text running out of its panel.
struct Plan {
    bool columns;
    float icon;        // Icon size, in design units.
    float gap;         // Space between panels.
    float titleSize;
    float bodySize;
    float lineHeight;
    float pad;         // Space above and below the text.
    float rowCap;      // The tallest a row grows when there is spare room.
};

const char* moveText() {
    switch (input().lastDevice) {
        case input::Device::Touch: return "Drag anywhere to run. Tap the DASH button to dodge.";
        case input::Device::Gamepad: return "Left stick to run. RB dashes, and makes you untouchable for a moment.";
        default: return "WASD or arrows to run. Shift or Space dashes, and makes you untouchable for a moment.";
    }
}

void drawGlyph(Glyph glyph, float cx, float cy, float size) {
    const Color cool = theme::kUiWeapon;
    switch (glyph) {
        case Glyph::Move: drawPassiveIcon(PassiveId::MoveSpeed, cx, cy, size, tint(cool)); break;
        case Glyph::Fire: drawWeaponIcon(WeaponId::Rifle, cx, cy, size, tint(cool)); break;
        case Glyph::Gem: drawGemIcon(cx, cy, size, tint(theme::kUiGem)); break;
        case Glyph::Cards: drawPassiveIcon(PassiveId::Damage, cx, cy, size, tint(theme::kUiGem)); break;
        case Glyph::Ammo: drawPickupIcon(PickupType::Ammo, cx, cy, size, tint(theme::kAmmoFill)); break;
        case Glyph::Evolve: drawWeaponIcon(WeaponId::Blades, cx, cy, size, tint(theme::kEvolution)); break;
        case Glyph::Pickups: drawPickupIcon(PickupType::Heal, cx, cy, size, tint(theme::kUiGem)); break;
        case Glyph::Goal: drawWeaponIcon(WeaponId::Sniper, cx, cy, size, tint(theme::kBossBar)); break;
    }
}

// Where text starts inside a panel, and the width left for it. A plan with no icon (the
// last resort for a very small or squarish window) gives the text the whole panel.
float textOffsetFor(float iconSize) { return iconSize > 0.0f ? 28.0f * ui::u() + iconSize * 1.32f : 14.0f * ui::u(); }

float textWidthFor(const Plan& plan, float panelWidth) { return panelWidth - textOffsetFor(plan.icon * ui::u()) - 14.0f * ui::u(); }

int bodyLines(const char* body, float width, const Plan& plan) {
    const float u = ui::u();
    return std::max(1, font::wrapped(body, 0.0f, 0.0f, width, plan.bodySize * u, plan.lineHeight * u, theme::kUiText, font::Align::Left, kMaxBodyLines, false));
}

// The height of the text block: the title, then the body lines.
float textBlockHeight(const Plan& plan, int lines) {
    const float u = ui::u();
    return (plan.titleSize + 5.0f) * u + static_cast<float>(lines) * plan.lineHeight * u;
}

float rowNeed(const Plan& plan, int lines) { return textBlockHeight(plan, lines) + plan.pad * 2.0f * ui::u(); }

void drawTip(const Tip& tip, const char* body, Rectangle r, const Plan& plan, int index) {
    const float u = ui::u();
    panel(r, withAlpha(theme::kUiPanel, 0.9f), theme::kUiBorder, 14.0f * u);
    const float icon = plan.icon > 0.0f ? std::min(r.height - plan.pad * 2.0f * u - 4.0f * u, plan.icon * u) : 0.0f;
    if (icon > 0.0f) {
        const float cx = r.x + 14.0f * u + icon * 0.66f;
        const float cy = r.y + r.height * 0.5f;
        panel(Rectangle{cx - icon * 0.66f, cy - icon * 0.66f, icon * 1.32f, icon * 1.32f}, theme::kUiWeaponFill, theme::kUiBorder, 0.0f);
        drawGlyph(tip.glyph, cx, cy, icon * 0.9f);
    }
    const float tx = r.x + textOffsetFor(icon);
    const float tw = r.x + r.width - 14.0f * u - tx;
    const int lines = bodyLines(body, tw, plan);
    const float block = textBlockHeight(plan, lines);
    const float top = r.y + (r.height - block) * 0.5f;
    text(tip.title, tx, top, plan.titleSize * u, theme::kUiAccent);
    font::wrapped(body, tx, top + (plan.titleSize + 5.0f) * u, tw, plan.bodySize * u, plan.lineHeight * u, tint(theme::kUiText), font::Align::Left, kMaxBodyLines, true);

    // The panel and the extent of its text, for the layout tests.
    char name[24];
    std::snprintf(name, sizeof(name), "Tip %d", index + 1);
    registerExtent(name, r);
    std::snprintf(name, sizeof(name), "Tip %d text", index + 1);
    registerExtent(name, Rectangle{tx, top, tw, block});
}

}  // namespace

bool drawHowToPlay() {
    const Layout& l = layout();
    const float u = l.u;
    DrawRectangle(0, 0, static_cast<int>(l.w), static_cast<int>(l.h), tint(theme::kScreenScrim));

    // Every number and recipe in the text comes from the data, so tuning never leaves
    // this screen telling the player the wrong rules.
    const balance::Evolution& recipe = balance::kEvolutions[0];
    char cards[110];
    char evolve[110];
    char goal[110];
    std::snprintf(cards, sizeof(cards), "Each level offers %d cards. You get %d rerolls per run if none fit.", kOffersPerLevel, balance::kStartingRerolls);
    std::snprintf(evolve, sizeof(evolve), "A level %d weapon plus its passive evolves. %s + %s = %s.", kMaxLevelPerItem, weaponName(recipe.weapon),
                  passiveName(recipe.passive), recipe.name);
    std::snprintf(goal, sizeof(goal), "Survive %d minutes. Then the boss arrives. Win or die, the run ends.", static_cast<int>(balance::kRunSeconds / 60.0f));
    const Tip tips[kTipCount] = {
        {Glyph::Move, "Move and dash", ""},
        {Glyph::Fire, "Weapons fire on their own", "Every gun shoots the nearest zombie. You only steer."},
        {Glyph::Gem, "Collect gems", "Green gems drop from zombies. Collect enough to level up."},
        {Glyph::Cards, "Pick a card", cards},
        {Glyph::Ammo, "Watch your ammo", "Guns reload by themselves. Reserve ammo runs out, so grab crates."},
        {Glyph::Evolve, "Evolve weapons", evolve},
        {Glyph::Pickups, "Pickups", "Hearts heal. Magnets pull in every gem. Bombs blast every zombie around you."},
        {Glyph::Goal, "The goal", goal},
    };
    const char* bodies[kTipCount];
    for (int i = 0; i < kTipCount; ++i) bodies[i] = i == 0 ? moveText() : tips[i].body;

    const float headerHeight = 60.0f * u;
    const float backHeight = tall(50.0f * u);
    const float areaTop = l.top + headerHeight;
    const float areaHeight = l.height() - headerHeight - backHeight - 14.0f * u;

    // The plans, roomiest first. Two columns need a window at least this wide.
    const bool columnsFit = l.width() >= 600.0f * u;
    const bool columnsFitText = l.width() >= 480.0f * u;
    const bool preferColumns = !l.portrait && l.width() >= 760.0f * u;
    const Plan roomy{false, 50.0f, 10.0f, 23.0f, 18.0f, 21.0f, 8.0f, 104.0f};
    const Plan roomyColumns{true, 50.0f, 10.0f, 23.0f, 18.0f, 21.0f, 8.0f, 96.0f};
    const Plan tight{false, 36.0f, 6.0f, 20.0f, 16.0f, 18.0f, 5.0f, 80.0f};
    const Plan tightColumns{true, 36.0f, 6.0f, 20.0f, 16.0f, 18.0f, 5.0f, 80.0f};
    const Plan textOnly{true, 0.0f, 6.0f, 20.0f, 16.0f, 18.0f, 5.0f, 80.0f};
    Plan candidates[6];
    int candidateCount = 0;
    if (preferColumns) candidates[candidateCount++] = roomyColumns;
    candidates[candidateCount++] = roomy;
    if (columnsFit && !preferColumns) candidates[candidateCount++] = roomyColumns;
    candidates[candidateCount++] = tight;
    if (columnsFit) candidates[candidateCount++] = tightColumns;
    if (columnsFitText) candidates[candidateCount++] = textOnly;  // The last resort: no icons.
    Plan chosen = candidates[0];
    float rowHeights[kTipCount];
    int rows = 0;
    float colWidth = 0.0f;
    float width = 0.0f;
    bool fits = false;

    auto evaluate = [&](const Plan& plan) {
        const int perRow = plan.columns ? 2 : 1;
        const int rowCount = kTipCount / perRow;
        const float planWidth = std::min(l.width(), plan.columns ? 960.0f * u : 620.0f * u);
        const float planCol = plan.columns ? (planWidth - plan.gap * u) * 0.5f : planWidth;
        float total = plan.gap * u * static_cast<float>(rowCount - 1);
        for (int r = 0; r < rowCount; ++r) {
            int most = 1;
            for (int c = 0; c < perRow; ++c) most = std::max(most, bodyLines(bodies[r * perRow + c], textWidthFor(plan, planCol), plan));
            rowHeights[r] = rowNeed(plan, most);
            total += rowHeights[r];
        }
        rows = rowCount;
        width = planWidth;
        colWidth = planCol;
        return total <= areaHeight;
    };
    // If nothing fits, the last (tightest) plan stays chosen and is drawn in whole rows.
    for (int i = 0; i < candidateCount && !fits; ++i) {
        chosen = candidates[i];
        fits = evaluate(chosen);
    }

    // Spare room makes rows a little taller, up to the plan's cap, so a big window is not cramped.
    float total = chosen.gap * u * static_cast<float>(rows - 1);
    for (int r = 0; r < rows; ++r) total += rowHeights[r];
    const float spare = std::max(0.0f, areaHeight - total);
    const float growEach = spare / static_cast<float>(rows);
    total = chosen.gap * u * static_cast<float>(rows - 1);
    for (int r = 0; r < rows; ++r) {
        rowHeights[r] += std::min(growEach, std::max(0.0f, chosen.rowCap * u - rowHeights[r]));
        total += rowHeights[r];
    }
    const float x = l.centerX() - width * 0.5f;
    float y = areaTop + std::max(0.0f, (areaHeight - total) * 0.5f);

    textShadowed("HOW TO PLAY", l.centerX(), l.top, 40.0f * u, theme::kUiAccent, font::Align::Center, 4.0f * u);

    const int perRow = chosen.columns ? 2 : 1;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < perRow; ++c) {
            const int i = r * perRow + c;
            drawTip(tips[i], bodies[i], Rectangle{x + static_cast<float>(c) * (colWidth + chosen.gap * u), y, colWidth, rowHeights[r]}, chosen, i);
        }
        y += rowHeights[r] + chosen.gap * u;
    }

    navigate(g_nav, 1);
    const Rectangle back{l.centerX() - 170.0f * u, l.bottom - backHeight, 340.0f * u, backHeight};
    return button(g_nav, 0, back, "Back", ButtonLook::Primary, promptBack());
}

}  // namespace ls::ui
