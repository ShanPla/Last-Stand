#include "components/Ui/PauseMenu/PauseMenu.h"

#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Names/Names.h"
#include "components/Ui/Widgets/Widgets.h"
#include "components/Weapons/Weapons.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ls::ui {

namespace {

Nav g_nav;

// What a passive is currently worth, as a short string such as "+32%".
void passiveEffect(const Player& player, PassiveId id, char* out, size_t size) {
    const PlayerStats& s = player.stats;
    switch (id) {
        case PassiveId::Damage: std::snprintf(out, size, "+%d%% damage", static_cast<int>(std::lround((s.damageMult - 1.0f) * 100.0f))); break;
        case PassiveId::FireRate: std::snprintf(out, size, "-%d%% cooldown", static_cast<int>(std::lround((1.0f - s.cooldownMult) * 100.0f))); break;
        case PassiveId::Area: std::snprintf(out, size, "+%d%% area", static_cast<int>(std::lround((s.areaMult - 1.0f) * 100.0f))); break;
        case PassiveId::MoveSpeed: std::snprintf(out, size, "+%d%% speed", static_cast<int>(std::lround((s.moveSpeed / balance::kPlayerSpeed - 1.0f) * 100.0f))); break;
        case PassiveId::PickupRange: std::snprintf(out, size, "+%d%% range", static_cast<int>(std::lround((s.pickupRange / balance::kPlayerPickupRange - 1.0f) * 100.0f))); break;
        case PassiveId::Armor: std::snprintf(out, size, "-%d per hit", static_cast<int>(std::lround(s.armor))); break;
        case PassiveId::Count: out[0] = '\0'; break;
    }
}

void drawWeaponRow(const GameState& state, const WeaponSlot& slot, Rectangle r, bool showStats) {
    const float u = ui::u();
    const balance::WeaponLevel stats = weapons::effectiveStats(state.localPlayer(), slot);
    const float cy = r.y + r.height * 0.5f;
    const float icon = std::min(r.height - 6.0f * u, 32.0f * u);
    const bool usesAmmo = stats.magazine > 0;
    const bool reloading = slot.reloadLeft > 0.0f;
    const bool dry = usesAmmo && slot.magazine <= 0 && slot.reserve <= 0 && !reloading;

    roundedRect(Rectangle{r.x, r.y + 1.0f * u, r.width, r.height - 2.0f * u}, 10.0f * u, withAlpha(theme::kUiBarBack, 0.35f));
    drawWeaponIcon(slot.id, r.x + 8.0f * u + icon * 0.5f, cy, icon, tint(dry ? theme::kIconDim : (slot.evolved ? theme::kEvolution : theme::kUiWeapon)));

    const float nameX = r.x + 16.0f * u + icon;
    char buffer[48];
    const float nameSize = 19.0f * u;
    const float rightEdge = r.x + r.width - 10.0f * u;

    // The ammo column, on the right.
    float ammoWidth = 0.0f;
    if (!usesAmmo) {
        text("No ammo needed", rightEdge, cy - 8.0f * u, 15.0f * u, theme::kUiTextDim, font::Align::Right);
        ammoWidth = font::measure("No ammo needed", 15.0f * u);
    } else if (dry) {
        textMiddle("EMPTY", rightEdge, cy, 20.0f * u, theme::kEmptyMark, font::Align::Right, 1.0f * u);
        ammoWidth = font::measure("EMPTY", 20.0f * u);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%d / %d", slot.magazine, stats.magazine);
        char reserve[24];
        std::snprintf(reserve, sizeof(reserve), "+ %d", slot.reserve);
        const float reserveWidth = font::measure("+ 000", 18.0f * u);
        textMiddle(reserve, rightEdge, cy, 18.0f * u, theme::kUiTextDim, font::Align::Right);
        textMiddle(buffer, rightEdge - reserveWidth - 8.0f * u, cy, 20.0f * u, reloading ? theme::kReload : theme::kAmmoFill, font::Align::Right);
        ammoWidth = reserveWidth + 8.0f * u + font::measure("000 / 000", 20.0f * u);
        if (reloading) {
            textMiddle("RELOADING", rightEdge - ammoWidth - 10.0f * u, cy, 14.0f * u, theme::kReload, font::Align::Right, 1.0f * u);
            ammoWidth += 10.0f * u + font::measure("RELOADING", 14.0f * u, 1.0f * u);
        }
    }

    // Name and level on the left, and exact numbers in the middle when there is room.
    const float room = rightEdge - ammoWidth - 12.0f * u - nameX;
    // The level tag follows the name, so without the stats column the name may take the rest.
    const float levelTagWidth = font::measure("Lv 5", 15.0f * u) + 12.0f * u;
    const float shown = font::fitSize(slotName(slot), nameSize, showStats ? room * 0.5f : room - levelTagWidth);
    textMiddle(slotName(slot), nameX, cy - 1.0f * u, shown, theme::kUiText);
    const float nameWidth = font::measure(slotName(slot), shown);
    std::snprintf(buffer, sizeof(buffer), "Lv %d", slot.level);
    textMiddle(buffer, nameX + nameWidth + 8.0f * u, cy, 15.0f * u, slot.evolved ? theme::kEvolution : theme::kUiTextDim);
    if (showStats) {
        const float dps = stats.cooldown > 0.0f ? 1.0f / stats.cooldown : 0.0f;
        std::snprintf(buffer, sizeof(buffer), "%d dmg   %.1f/s", static_cast<int>(std::lround(stats.damage)), static_cast<double>(dps));
        const float statsX = r.x + r.width * 0.45f;
        if (statsX + font::measure(buffer, 15.0f * u) < rightEdge - ammoWidth - 10.0f * u) {
            textMiddle(buffer, statsX, cy, 15.0f * u, theme::kUiTextDim);
        }
    }
}

// A squeezed window (a near-square one, say) uses shorter passive cells and weapon rows.
float passiveCellHeight(bool compact) { return (compact ? 26.0f : 30.0f) * ui::u(); }

// Everything in the build panel that is not a weapon row.
float buildFixedHeight(const Player& player, bool compact) {
    const float u = ui::u();
    const float passiveRows = static_cast<float>((player.passiveCount + 1) / 2);
    return 28.0f * u + 30.0f * u + 20.0f * u + 8.0f * u + (player.passiveCount > 0 ? 20.0f * u + passiveRows * passiveCellHeight(compact) : 0.0f);
}

// Weapon rows shrink a little when the window is short, but never below readable.
float buildRowHeight(const Player& player, float availableHeight, bool compact) {
    const float u = ui::u();
    const float wanted = (ui::layout().portrait ? 44.0f : 38.0f) * u;
    const float room = (availableHeight - buildFixedHeight(player, compact)) / static_cast<float>(std::max(1, player.weaponCount));
    return std::max((compact ? 22.0f : 26.0f) * u, std::min(wanted, room));
}

float buildHeight(const Player& player, float rowHeight, bool compact) {
    return buildFixedHeight(player, compact) + rowHeight * static_cast<float>(player.weaponCount);
}

void drawBuild(const GameState& state, Rectangle r, bool showStats, float rowHeight, bool compact) {
    const float u = ui::u();
    const Player& player = state.localPlayer();
    panel(r, theme::kUiPanel, theme::kUiBorder, 16.0f * u);
    const float pad = 14.0f * u;
    float y = r.y + pad;

    // Run summary line.
    char buffer[96];
    char clock[16];
    formatClock(clock, sizeof(clock), state.timeSeconds());
    std::snprintf(buffer, sizeof(buffer), "%s    %d kills    LV %d    HP %d/%d", clock, state.run.kills, player.level,
                  static_cast<int>(std::ceil(std::max(0.0f, player.hp))), static_cast<int>(player.maxHp));
    const float summarySize = font::fitSize(buffer, 20.0f * u, r.width - pad * 2.0f);
    text(buffer, r.x + pad, y, summarySize, theme::kUiText);
    y += summarySize + 10.0f * u;

    text("WEAPONS", r.x + pad, y, 14.0f * u, theme::kUiTextDim, font::Align::Left, 2.0f * u);
    text("LOADED / SIZE + RESERVE", r.x + r.width - pad, y, 14.0f * u, theme::kUiTextDim, font::Align::Right, 1.0f * u);
    y += 20.0f * u;
    for (int i = 0; i < player.weaponCount; ++i) {
        drawWeaponRow(state, player.weapons[i], Rectangle{r.x + pad * 0.6f, y, r.width - pad * 1.2f, rowHeight}, showStats);
        y += rowHeight;
    }
    y += 8.0f * u;

    if (player.passiveCount > 0) {
        text("PASSIVES", r.x + pad, y, 14.0f * u, theme::kUiTextDim, font::Align::Left, 2.0f * u);
        y += 20.0f * u;
        const float cellWidth = (r.width - pad * 1.2f) * 0.5f;
        const float cellHeight = passiveCellHeight(compact);
        for (int i = 0; i < player.passiveCount; ++i) {
            const PassiveSlot& passive = player.passives[i];
            const Rectangle cell{r.x + pad * 0.6f + static_cast<float>(i % 2) * cellWidth, y + static_cast<float>(i / 2) * cellHeight, cellWidth - 6.0f * u, cellHeight - 2.0f * u};
            drawPassiveIcon(passive.id, cell.x + 14.0f * u, cell.y + cell.height * 0.5f, 24.0f * u, tint(theme::kUiGem));
            std::snprintf(buffer, sizeof(buffer), "%s %d", passiveName(passive.id), passive.level);
            char effect[32];
            passiveEffect(player, passive.id, effect, sizeof(effect));
            // In a narrow panel the name and the full effect would run into each other. Every
            // effect then keeps only its number (the name already says what it is).
            const float textRoom = cell.width - 32.0f * u;
            if (textRoom < 200.0f * u) {
                for (char* c = effect; *c; ++c) {
                    if (*c == ' ') { *c = '\0'; break; }
                }
            }
            const float effectWidth = font::measure(effect, 16.0f * u);
            textMiddle(buffer, cell.x + 32.0f * u, cell.y + cell.height * 0.5f, font::fitSize(buffer, 17.0f * u, textRoom - effectWidth - 8.0f * u), theme::kUiText);
            textMiddle(effect, cell.x + cell.width, cell.y + cell.height * 0.5f, 16.0f * u, theme::kNewBest, font::Align::Right);
        }
        y += static_cast<float>((player.passiveCount + 1) / 2) * cellHeight;
    }
    // The panel, and how far its content really reaches, for the layout tests.
    registerExtent("Build", r);
    registerExtent("Build content", Rectangle{r.x, r.y, r.width, y - r.y});
}

// The level this run is on, in its own color, so it is clear what the run counts towards.
void drawLevelTag(Difficulty level, float x, float y, font::Align align) {
    const float u = ui::u();
    char label[24];
    std::snprintf(label, sizeof(label), "%s difficulty", difficultyName(level));
    text(label, x, y, 18.0f * u, theme::kDifficultyColors[static_cast<int>(level)], align, layout().px);
}

}  // namespace

void resetPauseMenu() { g_nav = Nav{}; }

PauseAction drawPauseMenu(const GameState& state, PauseKind kind) {
    const Layout& l = layout();
    const float u = l.u;
    PauseAction action = PauseAction::None;
    DrawRectangle(0, 0, static_cast<int>(l.w), static_cast<int>(l.h), tint(theme::kScreenScrim));

    // Two panes need room for the buttons and a readable build list. A 4:3 window is wide
    // enough for both, but not for the 760 design units the roomier look would want.
    const bool wide = !l.portrait && l.width() >= 690.0f * u;
    // In co-op, Quit leaves the room. Only the host can restart, and a guest's menu does not
    // pause anything, so it is not called paused.
    const bool coop = kind != PauseKind::Solo;
    const bool guest = kind == PauseKind::CoopGuest;
    const char* const labels[4] = {"Resume", "Settings", "Restart", coop ? "Leave Run" : "Quit to Title"};
    const char* title = guest ? "MENU" : "PAUSED";

    if (wide) {
        navigate(g_nav, 4);
        const float menuWidth = 290.0f * u;
        const float gap = 26.0f * u;
        const float buildWidth = std::min(l.width() - menuWidth - gap, 600.0f * u);
        const float total = menuWidth + gap + buildWidth;
        const float x = l.centerX() - total * 0.5f;
        const float rowHeight = buildRowHeight(state.localPlayer(), l.height(), false);
        const float height = buildHeight(state.localPlayer(), rowHeight, false);
        textShadowed(title, x, l.top + 4.0f * u, 46.0f * u, theme::kUiAccent, font::Align::Left, 4.0f * u);
        drawLevelTag(state.run.difficulty, x, l.top + 56.0f * u, font::Align::Left);
        float y = l.top + 86.0f * u;
        const float buttonHeight = tall(56.0f * u);
        for (int i = 0; i < 4; ++i) {
            const float h = i == 0 ? tall(64.0f * u) : buttonHeight;
            if (button(g_nav, i, Rectangle{x, y, menuWidth, h}, labels[i], i == 0 ? ButtonLook::Primary : ButtonLook::Normal, i == 0 ? promptPause() : nullptr, !(guest && i == 2))) {
                action = static_cast<PauseAction>(i + 1);
            }
            y += h + 12.0f * u;
        }
        drawBuild(state, Rectangle{x + menuWidth + gap, l.top + (l.height() - height) * 0.5f, buildWidth, height}, buildWidth >= 520.0f * u, rowHeight, false);
    } else {
        const float width = std::min(l.width(), 600.0f * u);
        const float x = l.centerX() - width * 0.5f;
        const float gap = 8.0f * u;
        const float between = 16.0f * u;

        // First the roomy layout: a column of four buttons under the build list.
        float buttonHeight = tall(52.0f * u);
        float buttonsHeight = buttonHeight * 4.0f + gap * 3.0f;
        float titleHeight = 72.0f * u;
        float availableTotal = l.height() - titleHeight - between - buttonsHeight;
        bool compact = false;
        bool grid = false;
        // If the whole build list can't fit above the buttons (a window that is nearly square),
        // the buttons go two by two and the list gets shorter rows, so nothing runs under them.
        if (buildHeight(state.localPlayer(), 26.0f * u, false) > availableTotal) {
            compact = true;
            grid = true;
            buttonHeight = tall(48.0f * u);
            buttonsHeight = buttonHeight * 2.0f + gap;
            titleHeight = 56.0f * u;
            availableTotal = l.height() - titleHeight - between - buttonsHeight;
        }
        navigate(g_nav, 4, grid);  // Two by two buttons also move with left and right.
        // Title, build and buttons form one group, centered, so a tall phone has no big hole.
        const float rowHeight = buildRowHeight(state.localPlayer(), availableTotal, compact);
        const float panelHeight = std::min(availableTotal, buildHeight(state.localPlayer(), rowHeight, compact));
        const float groupTop = l.top + (l.height() - (titleHeight + panelHeight + between + buttonsHeight)) * 0.5f;
        textShadowed(title, l.centerX(), groupTop, (compact ? 34.0f : 40.0f) * u, theme::kUiAccent, font::Align::Center, 4.0f * u);
        drawLevelTag(state.run.difficulty, l.centerX(), groupTop + (compact ? 36.0f : 44.0f) * u, font::Align::Center);
        const float buildTop = groupTop + titleHeight;
        const float buttonsTop = buildTop + panelHeight + between;
        drawBuild(state, Rectangle{x, buildTop, width, panelHeight}, false, rowHeight, compact);
        for (int i = 0; i < 4; ++i) {
            Rectangle rect{x, buttonsTop + static_cast<float>(i) * (buttonHeight + gap), width, buttonHeight};
            if (grid) {
                const float cell = (width - gap) * 0.5f;
                rect = Rectangle{x + static_cast<float>(i % 2) * (cell + gap), buttonsTop + static_cast<float>(i / 2) * (buttonHeight + gap), cell, buttonHeight};
            }
            if (button(g_nav, i, rect, labels[i], i == 0 ? ButtonLook::Primary : ButtonLook::Normal, i == 0 ? promptPause() : nullptr, !(guest && i == 2))) {
                action = static_cast<PauseAction>(i + 1);
            }
        }
    }
    return action;
}

}  // namespace ls::ui
