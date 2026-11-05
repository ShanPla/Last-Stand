#include "components/Ui/HowToPlay/HowToPlay.h"

#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Widgets/Widgets.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

Nav g_nav;

enum class Glyph { Move, Fire, Gem, Cards, Ammo, Evolve, Pickups, Goal };

struct Tip {
    Glyph glyph;
    const char* title;
    const char* body;
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

void drawTip(const Tip& tip, const char* body, Rectangle r) {
    const float u = ui::u();
    panel(r, withAlpha(theme::kUiPanel, 0.9f), theme::kUiBorder, 14.0f * u);
    const float icon = std::min(r.height - 20.0f * u, 50.0f * u);
    const float cx = r.x + 14.0f * u + icon * 0.66f;
    const float cy = r.y + r.height * 0.5f;
    panel(Rectangle{cx - icon * 0.66f, cy - icon * 0.66f, icon * 1.32f, icon * 1.32f}, theme::kUiWeaponFill, theme::kUiBorder, 0.0f);
    drawGlyph(tip.glyph, cx, cy, icon * 0.9f);
    const float tx = cx + icon * 0.66f + 14.0f * u;
    const float tw = r.x + r.width - 14.0f * u - tx;
    text(tip.title, tx, r.y + 8.0f * u, 23.0f * u, theme::kUiAccent);
    font::wrapped(body, tx, r.y + 8.0f * u + 28.0f * u, tw, 18.0f * u, 21.0f * u, tint(theme::kUiText), font::Align::Left, 2, true);
}

}  // namespace

bool drawHowToPlay() {
    const Layout& l = layout();
    const float u = l.u;
    DrawRectangle(0, 0, static_cast<int>(l.w), static_cast<int>(l.h), tint(theme::kScreenScrim));

    char eight[110];
    std::snprintf(eight, sizeof(eight), "Survive %d minutes. Then the boss arrives. Win or die, the run ends.", static_cast<int>(balance::kRunSeconds / 60.0f));
    const Tip tips[] = {
        {Glyph::Move, "Move and dash", ""},
        {Glyph::Fire, "Weapons fire on their own", "Every gun shoots the nearest zombie. You only steer."},
        {Glyph::Gem, "Collect gems", "Green gems drop from zombies. Collect enough to level up."},
        {Glyph::Cards, "Pick a card", "Each level offers 3 cards. Reroll twice per run if none fit."},
        {Glyph::Ammo, "Watch your ammo", "Guns reload by themselves. Reserve ammo runs out, so grab crates."},
        {Glyph::Evolve, "Evolve weapons", "A level 5 weapon plus its passive evolves. Rifle + Fire Rate = Storm Rifle."},
        {Glyph::Pickups, "Pickups", "Hearts heal. Magnets pull in every gem. Bombs blast every zombie around you."},
        {Glyph::Goal, "The goal", eight},
    };
    constexpr int kTipCount = 8;

    const bool columns = !l.portrait && l.width() >= 760.0f * u;
    const float headerHeight = 66.0f * u;
    const float backHeight = tall(50.0f * u);
    const float gap = 10.0f * u;
    const float areaTop = l.top + headerHeight;
    const float areaHeight = l.height() - headerHeight - backHeight - 14.0f * u;
    const int rows = columns ? kTipCount / 2 : kTipCount;
    const float rowHeight = std::min(columns ? 96.0f * u : 104.0f * u, (areaHeight - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows));
    const float width = std::min(l.width(), columns ? 960.0f * u : 620.0f * u);
    const float x = l.centerX() - width * 0.5f;
    const float colWidth = columns ? (width - gap) * 0.5f : width;
    const float totalHeight = rowHeight * static_cast<float>(rows) + gap * static_cast<float>(rows - 1);
    const float startY = areaTop + std::max(0.0f, (areaHeight - totalHeight) * 0.5f);

    textShadowed("HOW TO PLAY", l.centerX(), l.top, 40.0f * u, theme::kUiAccent, font::Align::Center, 4.0f * u);

    for (int i = 0; i < kTipCount; ++i) {
        const int column = columns ? i % 2 : 0;
        const int row = columns ? i / 2 : i;
        const Rectangle r{x + static_cast<float>(column) * (colWidth + gap), startY + static_cast<float>(row) * (rowHeight + gap), colWidth, rowHeight};
        drawTip(tips[i], i == 0 ? moveText() : tips[i].body, r);
    }

    navigate(g_nav, 1);
    const Rectangle back{l.centerX() - 170.0f * u, l.bottom - backHeight, 340.0f * u, backHeight};
    return button(g_nav, 0, back, "Back", ButtonLook::Primary, promptBack());
}

}  // namespace ls::ui
