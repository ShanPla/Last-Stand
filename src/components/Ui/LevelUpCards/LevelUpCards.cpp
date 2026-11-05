#include "components/Ui/LevelUpCards/LevelUpCards.h"

#include "components/Audio/Audio.h"
#include "components/Ui/BuildStrip/BuildStrip.h"
#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Names/Names.h"
#include "components/Ui/Widgets/Widgets.h"
#include "data/Balance.h"
#include "data/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ls::ui {

namespace {

constexpr float kInputDelaySeconds = 0.35f;  // Stops a held key or fast tap choosing by accident.
constexpr int kMaxRows = 5;
// Slot sizes of the held build shown under the cards, in design units. The layout reserves
// room with the same numbers the strip is drawn with.
constexpr float kBuildWeaponSlot = 34.0f;
constexpr float kBuildPassiveSlot = 28.0f;
constexpr float kBuildStripWidth = 430.0f;  // The widest strip: six weapons and six passives.
constexpr int kWeaponTypes = static_cast<int>(WeaponId::Count);

float g_openTime = 0.0f;
Nav g_nav;

// ---- What a card says -----------------------------------------------------------

struct StatRow {
    char label[24] = "";
    char from[20] = "";
    char to[20] = "";
    bool hasFrom = false;
};

struct CardInfo {
    const char* tag = "";
    Color tagColor = theme::kUiWeapon;
    const char* name = "";
    const char* blurb = "";
    int fromLevel = 0;  // 0 when the item is new.
    int toLevel = 1;
    bool showLevel = true;
    StatRow rows[kMaxRows];
    int rowCount = 0;
    char hint[140] = "";
    char blurbText[72] = "";  // Backing text for blurbs built at run time.
};

const char* const kWeaponBlurbs[kWeaponTypes] = {
    "Aimed bursts at the nearest zombie.",
    "A wide cone of short-range pellets.",
    "Very fast, weak bullets with scatter.",
    "An instant beam through every zombie in a line.",
    "A stream of flames that burn through zombies.",
    "Lobbed grenades that explode when the fuse ends.",
    "Missiles that home in on zombies.",
    "Mines that blow up when a zombie steps close.",
    "Lightning that chains between zombies. No ammo.",
    "Blades circle you and cut what comes close. No ammo.",
};

const char* const kPassiveBlurbs[] = {
    "Every weapon hits harder.",
    "Every gun fires faster.",
    "Bigger blasts, flames and blade circles.",
    "Run faster.",
    "Gems and drops fly to you from further away.",
    "Every hit on you does less damage.",
};

const char* countLabel(WeaponId id) {
    switch (id) {
        case WeaponId::Rifle: case WeaponId::Smg: return "Bullets";
        case WeaponId::Shotgun: return "Pellets";
        case WeaponId::Sniper: return "Beams";
        case WeaponId::Flamethrower: return "Streams";
        case WeaponId::GrenadeLauncher: return "Grenades";
        case WeaponId::Rockets: return "Rockets";
        case WeaponId::Landmines: return "Mines";
        case WeaponId::Tesla: return "Chain jumps";
        case WeaponId::Blades: return "Blades";
        default: return "Count";
    }
}

const char* areaLabel(WeaponId id) {
    switch (id) {
        case WeaponId::Sniper: return "Beam width";
        case WeaponId::Flamethrower: return "Flame size";
        case WeaponId::GrenadeLauncher: case WeaponId::Rockets: case WeaponId::Landmines: return "Blast radius";
        case WeaponId::Tesla: return "Jump range";
        case WeaponId::Blades: return "Blade reach";
        default: return nullptr;
    }
}

enum class Fmt { Whole, PerSecond, Seconds };

void format(char* out, size_t size, Fmt fmt, float value) {
    switch (fmt) {
        case Fmt::Whole: std::snprintf(out, size, "%d", static_cast<int>(std::lround(value))); break;
        case Fmt::PerSecond: std::snprintf(out, size, "%.1f/s", static_cast<double>(value)); break;
        case Fmt::Seconds: std::snprintf(out, size, "%.2fs", static_cast<double>(value)); break;
    }
}

// Adds a row. For an upgrade, a stat that does not change is left out.
void addRow(CardInfo& info, const char* label, Fmt fmt, float from, float to, bool isNew) {
    if (info.rowCount >= kMaxRows || !label) return;
    if (!isNew && std::fabs(from - to) < 1e-4f) return;
    StatRow& row = info.rows[info.rowCount++];
    std::snprintf(row.label, sizeof(row.label), "%s", label);
    format(row.to, sizeof(row.to), fmt, to);
    row.hasFrom = !isNew;
    if (!isNew) format(row.from, sizeof(row.from), fmt, from);
}

void addWeaponRows(CardInfo& info, WeaponId id, int fromLevel, int toLevel) {
    const bool isNew = fromLevel == 0;
    const balance::WeaponLevel& to = balance::kWeaponLevels[static_cast<int>(id)][toLevel - 1];
    const balance::WeaponLevel& from = balance::kWeaponLevels[static_cast<int>(id)][std::max(0, fromLevel - 1)];
    addRow(info, "Damage", Fmt::Whole, from.damage, to.damage, isNew);
    addRow(info, countLabel(id), Fmt::Whole, static_cast<float>(from.count), static_cast<float>(to.count), isNew);
    if (to.magazine > 0) addRow(info, "Magazine", Fmt::Whole, static_cast<float>(from.magazine), static_cast<float>(to.magazine), isNew);
    if (to.cooldown > 0.0f) addRow(info, "Fire rate", Fmt::PerSecond, 1.0f / from.cooldown, 1.0f / to.cooldown, isNew);
    addRow(info, areaLabel(id), Fmt::Whole, from.area, to.area, isNew);
    if (to.pierce > 0 || from.pierce > 0) addRow(info, "Pierce", Fmt::Whole, static_cast<float>(from.pierce), static_cast<float>(to.pierce), isNew);
    if (to.maxReserve > 0) addRow(info, "Reserve ammo", Fmt::Whole, static_cast<float>(from.maxReserve), static_cast<float>(to.maxReserve), isNew);
    if (to.reload > 0.0f) addRow(info, "Reload", Fmt::Seconds, from.reload, to.reload, isNew);
}

void addPercentRow(CardInfo& info, const char* label, float fromMult, float toMult, bool isNew, bool reduction) {
    if (info.rowCount >= kMaxRows) return;
    StatRow& row = info.rows[info.rowCount++];
    std::snprintf(row.label, sizeof(row.label), "%s", label);
    const auto text = [&](char* out, float mult) {
        const int percent = static_cast<int>(std::lround(reduction ? (1.0f - mult) * 100.0f : (mult - 1.0f) * 100.0f));
        std::snprintf(out, 20, reduction ? "-%d%%" : "+%d%%", percent);
    };
    text(row.to, toMult);
    row.hasFrom = !isNew;
    if (!isNew) text(row.from, fromMult);
}

void addPassiveRows(CardInfo& info, PassiveId id, int fromLevel, int toLevel) {
    const bool isNew = fromLevel == 0;
    const int a = std::max(0, fromLevel - 1);
    const int b = toLevel - 1;
    switch (id) {
        case PassiveId::Damage: addPercentRow(info, "Damage", balance::kPassiveDamage[a], balance::kPassiveDamage[b], isNew, false); break;
        case PassiveId::FireRate: addPercentRow(info, "Cooldown", balance::kPassiveCooldown[a], balance::kPassiveCooldown[b], isNew, true); break;
        case PassiveId::Area: addPercentRow(info, "Area", balance::kPassiveArea[a], balance::kPassiveArea[b], isNew, false); break;
        case PassiveId::MoveSpeed: addPercentRow(info, "Move speed", balance::kPassiveMoveSpeed[a], balance::kPassiveMoveSpeed[b], isNew, false); break;
        case PassiveId::PickupRange: addPercentRow(info, "Pickup range", balance::kPassivePickupRange[a], balance::kPassivePickupRange[b], isNew, false); break;
        case PassiveId::Armor: addRow(info, "Armor", Fmt::Whole, balance::kPassiveArmor[a], balance::kPassiveArmor[b], isNew); break;
        case PassiveId::Count: break;
    }
}

int heldWeaponLevel(const Player& player, WeaponId id) {
    for (int i = 0; i < player.weaponCount; ++i) {
        if (player.weapons[i].id == id) return player.weapons[i].level;
    }
    return 0;
}

int heldPassiveLevel(const Player& player, PassiveId id) {
    for (int i = 0; i < player.passiveCount; ++i) {
        if (player.passives[i].id == id) return player.passives[i].level;
    }
    return 0;
}

void describe(const GameState& state, const Card& card, CardInfo& info) {
    const Player& player = state.player;
    switch (card.kind) {
        case CardKind::Weapon: {
            const int from = heldWeaponLevel(player, card.weapon);
            info.fromLevel = from;
            info.toLevel = card.newLevel;
            info.name = weaponName(card.weapon);
            info.tag = from == 0 ? "NEW WEAPON" : "WEAPON UPGRADE";
            info.tagColor = theme::kUiWeapon;
            if (from == 0) info.blurb = kWeaponBlurbs[static_cast<int>(card.weapon)];
            addWeaponRows(info, card.weapon, from, card.newLevel);
            if (const balance::Evolution* evolution = evolutionForWeapon(card.weapon)) {
                std::snprintf(info.hint, sizeof(info.hint), "%s at level %d with %s evolves into %s", weaponName(card.weapon),
                              kMaxLevelPerItem, passiveName(evolution->passive), evolution->name);
            }
            break;
        }
        case CardKind::Passive: {
            const int from = heldPassiveLevel(player, card.passive);
            info.fromLevel = from;
            info.toLevel = card.newLevel;
            info.name = passiveName(card.passive);
            info.tag = from == 0 ? "NEW PASSIVE" : "PASSIVE UPGRADE";
            info.tagColor = theme::kUiGem;
            info.blurb = kPassiveBlurbs[static_cast<int>(card.passive)];
            addPassiveRows(info, card.passive, from, card.newLevel);
            if (const balance::Evolution* evolution = evolutionForPassive(card.passive)) {
                std::snprintf(info.hint, sizeof(info.hint), "%s at level %d with %s evolves into %s", weaponName(evolution->weapon),
                              kMaxLevelPerItem, passiveName(card.passive), evolution->name);
            }
            break;
        }
        case CardKind::Evolution: {
            info.tag = "EVOLUTION";
            info.tagColor = theme::kEvolution;
            info.showLevel = false;
            info.name = "Evolution";
            if (const balance::Evolution* evolution = evolutionForWeapon(card.weapon)) {
                info.name = evolution->name;
                std::snprintf(info.blurbText, sizeof(info.blurbText), "Your %s becomes a %s.", weaponName(card.weapon), evolution->name);
                info.blurb = info.blurbText;
                StatRow* row = nullptr;
                const auto add = [&](const char* label) {
                    row = &info.rows[info.rowCount++];
                    std::snprintf(row->label, sizeof(row->label), "%s", label);
                    row->hasFrom = false;
                };
                if (evolution->damageMult != 1.0f) {
                    add("Damage");
                    std::snprintf(row->to, sizeof(row->to), "+%d%%", static_cast<int>(std::lround((evolution->damageMult - 1.0f) * 100.0f)));
                }
                if (evolution->cooldownMult != 1.0f) {
                    add("Fire rate");
                    std::snprintf(row->to, sizeof(row->to), "+%d%%", static_cast<int>(std::lround((1.0f / evolution->cooldownMult - 1.0f) * 100.0f)));
                }
                if (evolution->countBonus > 0) {
                    add(countLabel(card.weapon));
                    std::snprintf(row->to, sizeof(row->to), "+%d", evolution->countBonus);
                }
                if (evolution->areaMult != 1.0f) {
                    add("Area");
                    std::snprintf(row->to, sizeof(row->to), "+%d%%", static_cast<int>(std::lround((evolution->areaMult - 1.0f) * 100.0f)));
                }
                if (evolution->pierceBonus > 0 && info.rowCount < kMaxRows) {
                    add("Pierce");
                    std::snprintf(row->to, sizeof(row->to), "+%d", evolution->pierceBonus);
                }
            }
            break;
        }
        case CardKind::Heal: {
            info.tag = "RECOVER";
            info.tagColor = theme::kUiGem;
            info.name = "Heal";
            info.showLevel = false;
            info.blurb = "Your build is complete. Restore some health.";
            StatRow& row = info.rows[info.rowCount++];
            std::snprintf(row.label, sizeof(row.label), "Health");
            std::snprintf(row.to, sizeof(row.to), "+%d%%", static_cast<int>(std::lround(balance::kHealCardFraction * 100.0f)));
            break;
        }
    }
}

// ---- Drawing helpers -------------------------------------------------------------

// A pixel arrow: a short shaft and a three-step head, pointing right.
void drawArrow(float cx, float cy, float size, Color color) {
    const float p = std::max(layout().px, snapPx(size / 6.0f));
    const float shaft = snapPx(size * 0.62f);
    pixelRect(Rectangle{cx - size * 0.5f, cy - p * 0.5f, shaft, p}, color);
    pixelArrow(cx + size * 0.5f - p * 3.0f, cy, p * 5.0f, color);
}

// "from > to", right aligned on rightX. Returns the width used.
float fromTo(float rightX, float cy, float size, const char* from, const char* to, bool hasFrom, Color toColor) {
    textMiddle(to, rightX, cy, size, toColor, font::Align::Right);
    const float toWidth = font::measure(to, size);
    if (!hasFrom) return toWidth;
    const float arrow = size * 0.8f;
    const float arrowCenter = rightX - toWidth - size * 0.3f - arrow * 0.5f;
    drawArrow(arrowCenter, cy, arrow, theme::kUiTextDim);
    const float fromRight = arrowCenter - arrow * 0.5f - size * 0.3f;
    textMiddle(from, fromRight, cy, size, theme::kUiTextDim, font::Align::Right);
    return toWidth + size * 0.6f + arrow + font::measure(from, size);
}

void drawIcon(const Card& card, float cx, float cy, float size, Color color) {
    switch (card.kind) {
        case CardKind::Weapon: case CardKind::Evolution: drawWeaponIcon(card.weapon, cx, cy, size, color); break;
        case CardKind::Passive: drawPassiveIcon(card.passive, cx, cy, size, color); break;
        case CardKind::Heal: drawPickupIcon(PickupType::Heal, cx, cy, size, color); break;
    }
}

void drawIconBadge(const Card& card, const CardInfo& info, float cx, float cy, float size) {
    // A square frame in the card's color, with the icon inside.
    panel(Rectangle{cx - size * 0.5f, cy - size * 0.5f, size, size}, card.kind == CardKind::Evolution ? theme::kEvolutionFill : theme::kUiWeaponFill,
          info.tagColor, 0.0f);
    drawIcon(card, cx, cy, size * 0.8f, tint(info.tagColor));
}

// Five squares for the five levels. Filled up to the new level, the new one bright.
void drawPips(float cx, float cy, float radius, int fromLevel, int toLevel, Color color) {
    const float spacing = snapPx(radius * 3.2f);
    const float side = snapPx(radius * 1.8f);
    const float start = cx - spacing * 2.0f - side * 0.5f;
    for (int i = 0; i < kMaxLevelPerItem; ++i) {
        const float x = start + spacing * static_cast<float>(i);
        const bool owned = i < fromLevel;
        const bool gained = i >= fromLevel && i < toLevel;
        pixelRect(Rectangle{x - layout().px, cy - side * 0.5f - layout().px, side + layout().px * 2.0f, side + layout().px * 2.0f}, theme::kUiBorderDark);
        pixelRect(Rectangle{x, cy - side * 0.5f, side, side}, owned ? color : (gained ? theme::kUiText : theme::kUiBarBack));
    }
}

// ---- Layout ----------------------------------------------------------------------

struct Metrics {
    bool columns = true;
    Rectangle cards[kOffersPerLevel];
    Rectangle reroll{};
    float buildX = 0.0f;
    float buildY = 0.0f;
    float titleY = 0.0f;
};

Metrics computeMetrics(const Layout& l, int count) {
    Metrics m;
    const float u = l.u;
    m.columns = !l.portrait && l.width() >= 760.0f * u;
    m.titleY = l.top;
    const float gap = 16.0f * u;
    if (m.columns) {
        const float top = l.top + 84.0f * u;
        const float rowH = tall(52.0f * u);
        const float rowY = l.bottom - rowH;
        const float availableH = rowY - 14.0f * u - top;
        const float cardH = std::max(200.0f * u, std::min(380.0f * u, availableH));
        const float cardW = std::min(310.0f * u, (l.width() - gap * static_cast<float>(count - 1)) / static_cast<float>(count));
        const float total = cardW * static_cast<float>(count) + gap * static_cast<float>(count - 1);
        const float startX = l.centerX() - total * 0.5f;
        const float startY = top + std::max(0.0f, (availableH - cardH) * 0.5f);
        for (int i = 0; i < count; ++i) m.cards[i] = Rectangle{startX + static_cast<float>(i) * (cardW + gap), startY, cardW, cardH};
        m.reroll = Rectangle{startX + total - 230.0f * u, rowY, 230.0f * u, rowH};
        m.buildX = startX;
        m.buildY = rowY + (rowH - weaponSlotsHeight(kBuildWeaponSlot * u)) * 0.5f;
    } else {
        const float top = l.top + 70.0f * u;
        const float rerollH = tall(46.0f * u);
        const float buildH = weaponSlotsHeight(kBuildWeaponSlot * u);
        // On a landscape window that is wide enough, the reroll button shares a row with the
        // build strip. Under it, a 4:3 screen has no room left for three whole cards.
        const float besideWidth = std::min(l.width(), 660.0f * u);
        const float besideReroll = besideWidth - (kBuildStripWidth + 12.0f) * u;
        const bool beside = !l.portrait && besideReroll >= 150.0f * u;
        const float rowH = beside ? std::max(rerollH, buildH) : rerollH + 10.0f * u + buildH;
        const float availableH = l.bottom - (rowH + 6.0f * u) - top;
        const float rowGap = 12.0f * u;
        const float cardH = std::max(108.0f * u, std::min(230.0f * u, (availableH - rowGap * static_cast<float>(count - 1)) / static_cast<float>(count)));
        const float cardW = beside ? besideWidth : std::min(l.width(), 620.0f * u);
        const float x = l.centerX() - cardW * 0.5f;
        const float stack = cardH * static_cast<float>(count) + rowGap * static_cast<float>(count - 1);
        const float startY = top + std::max(0.0f, (availableH - stack) * 0.5f);
        for (int i = 0; i < count; ++i) m.cards[i] = Rectangle{x, startY + static_cast<float>(i) * (cardH + rowGap), cardW, cardH};
        const float below = startY + stack + 12.0f * u;
        if (beside) {
            const float rerollW = std::min(230.0f * u, besideReroll);
            m.reroll = Rectangle{x + cardW - rerollW, below + (rowH - rerollH) * 0.5f, rerollW, rerollH};
            m.buildX = x;
            m.buildY = below + (rowH - buildH) * 0.5f;
        } else {
            m.reroll = Rectangle{x, below, cardW, rerollH};
            m.buildX = x;
            m.buildY = below + rerollH + 10.0f * u;
        }
    }
    return m;
}

// ---- One card --------------------------------------------------------------------

void drawCard(const Card& card, const CardInfo& info, Rectangle r, bool columns, bool selected, float ready) {
    const float u = ui::u();
    const bool evolution = card.kind == CardKind::Evolution;
    const Color accent = evolution ? theme::kEvolution : theme::kUiAccent;
    const float dim = 0.55f + 0.45f * ready;  // Cards brighten while the input delay runs out.

    // The selected card gets a hard gold frame outside its own border.
    const float p = layout().px;
    if (selected && ready >= 1.0f) roundedRect(Rectangle{r.x - 2.0f * p, r.y - 2.0f * p, r.width + 4.0f * p, r.height + 4.0f * p}, 0.0f, accent);
    panel(r, withAlpha(evolution ? theme::kEvolutionFill : theme::kUiPanel, dim), evolution ? theme::kEvolution : theme::kUiBorder, 0.0f);

    const float pad = 16.0f * u;
    char level[24];
    const Color valueColor = theme::kNewBest;
    const float bodyAlpha = dim;

    if (columns) {
        const float cx = r.x + r.width * 0.5f;
        textMiddle(info.tag, cx, r.y + pad + 7.0f * u, 15.0f * u, withAlpha(info.tagColor, bodyAlpha), font::Align::Center, 2.0f * u);
        const float iconSize = std::min(78.0f * u, r.height * 0.24f);
        const float iconY = r.y + pad + 22.0f * u + iconSize * 0.5f;
        drawIconBadge(card, info, cx, iconY, iconSize);
        float y = iconY + iconSize * 0.5f + 8.0f * u;
        const float nameSize = font::fitSize(info.name, 30.0f * u, r.width - pad * 2.0f);
        text(info.name, cx, y, nameSize, withAlpha(theme::kUiText, bodyAlpha), font::Align::Center);
        y += nameSize + 4.0f * u;
        if (info.showLevel) {
            std::snprintf(level, sizeof(level), "%d", info.toLevel);
            drawPips(cx, y + 6.0f * u, 4.5f * u, info.fromLevel, info.toLevel, theme::kUiAccent);
            y += 18.0f * u;
            char from[8];
            std::snprintf(from, sizeof(from), "%d", info.fromLevel);
            const char* label = info.fromLevel == 0 ? "NEW  LEVEL 1" : "LEVEL";
            if (info.fromLevel == 0) {
                textMiddle(label, cx, y + 8.0f * u, 15.0f * u, withAlpha(theme::kUiTextDim, bodyAlpha), font::Align::Center, 1.5f * u);
            } else {
                const float lw = font::measure(label, 15.0f * u, 1.5f * u);
                const float width = lw + 12.0f * u + 40.0f * u;
                const float left = cx - width * 0.5f;
                textMiddle(label, left, y + 8.0f * u, 15.0f * u, withAlpha(theme::kUiTextDim, bodyAlpha), font::Align::Left, 1.5f * u);
                fromTo(left + width, y + 8.0f * u, 17.0f * u, from, level, true, theme::kUiText);
            }
            y += 22.0f * u;
        }
        y += 2.0f * u;

        // The hint is anchored to the bottom, and the rows use what is left above it.
        float hintHeight = 0.0f;
        const float hintSize = 16.0f * u;
        int hintLines = 0;
        if (info.hint[0]) {
            hintLines = font::wrapped(info.hint, 0, 0, r.width - pad * 1.2f - 16.0f * u, hintSize, hintSize * 1.2f, theme::kEvolution, font::Align::Left, 3, false);
            hintHeight = static_cast<float>(hintLines) * hintSize * 1.2f + 14.0f * u;
        }
        const float hintTop = r.y + r.height - pad * 0.7f - hintHeight;
        if (info.blurb[0] && (info.rowCount <= 3 || card.kind == CardKind::Evolution)) {
            const float blurbSize = 16.0f * u;
            const int lines = font::wrapped(info.blurb, cx, y, r.width - pad * 2.0f, blurbSize, blurbSize * 1.2f, theme::kUiTextDim, font::Align::Center, 2, false);
            const float need = static_cast<float>(lines) * blurbSize * 1.2f + 6.0f * u;
            if (y + need < hintTop - 24.0f * u) {
                font::wrapped(info.blurb, cx, y, r.width - pad * 2.0f, blurbSize, blurbSize * 1.2f, tint(withAlpha(theme::kUiTextDim, bodyAlpha)), font::Align::Center, 2, true);
                y += need;
            }
        }
        DrawRectangle(static_cast<int>(r.x + pad), static_cast<int>(y), static_cast<int>(r.width - pad * 2.0f), std::max(1, static_cast<int>(u)), tint(theme::kDivider));
        y += 8.0f * u;
        const float rowHeight = 27.0f * u;
        for (int i = 0; i < info.rowCount; ++i) {
            if (y + rowHeight > hintTop - 4.0f * u) break;
            const StatRow& row = info.rows[i];
            textMiddle(row.label, r.x + pad, y + rowHeight * 0.5f, 18.0f * u, withAlpha(theme::kUiTextDim, bodyAlpha));
            fromTo(r.x + r.width - pad, y + rowHeight * 0.5f, 20.0f * u, row.from, row.to, row.hasFrom, withAlpha(valueColor, bodyAlpha));
            y += rowHeight;
        }
        if (hintLines > 0) {
            const Rectangle plate{r.x + pad * 0.6f, hintTop, r.width - pad * 1.2f, hintHeight};
            roundedRect(plate, 8.0f * u, withAlpha(theme::kEvolutionFill, 0.9f));
            roundedOutline(plate, 8.0f * u, 1.2f * u, withAlpha(theme::kEvolution, 0.55f));
            font::wrapped(info.hint, plate.x + 8.0f * u, plate.y + 7.0f * u, plate.width - 16.0f * u, hintSize, hintSize * 1.2f,
                          tint(theme::kEvolution), font::Align::Left, 3, true);
        }
    } else {
        // A wide, short card for portrait screens: icon on the left, text on the right.
        const float iconSize = std::min(r.height - pad * 1.4f, 70.0f * u);
        const float iconCx = r.x + pad + iconSize * 0.5f;
        drawIconBadge(card, info, iconCx, r.y + pad * 0.7f + iconSize * 0.5f, iconSize);
        if (info.showLevel) {
            drawPips(iconCx, r.y + pad * 0.7f + iconSize + 10.0f * u, 3.6f * u, info.fromLevel, info.toLevel, theme::kUiAccent);
        }
        const float tx = r.x + pad + iconSize + 14.0f * u;
        const float tw = r.x + r.width - pad - tx;
        textMiddle(info.tag, tx, r.y + pad * 0.7f + 7.0f * u, 14.0f * u, withAlpha(info.tagColor, bodyAlpha), font::Align::Left, 1.6f * u);
        if (info.showLevel) {
            char from[8];
            std::snprintf(from, sizeof(from), "%d", info.fromLevel);
            std::snprintf(level, sizeof(level), "%d", info.toLevel);
            if (info.fromLevel == 0) {
                char text[24];
                std::snprintf(text, sizeof(text), "LEVEL 1");
                textMiddle(text, r.x + r.width - pad, r.y + pad * 0.7f + 7.0f * u, 15.0f * u, withAlpha(theme::kUiTextDim, bodyAlpha), font::Align::Right);
            } else {
                fromTo(r.x + r.width - pad, r.y + pad * 0.7f + 7.0f * u, 16.0f * u, from, level, true, theme::kUiText);
                textMiddle("LEVEL", r.x + r.width - pad - 80.0f * u, r.y + pad * 0.7f + 7.0f * u, 14.0f * u, withAlpha(theme::kUiTextDim, bodyAlpha), font::Align::Right, 1.4f * u);
            }
        }
        float y = r.y + pad * 0.7f + 18.0f * u;
        const float nameSize = font::fitSize(info.name, 28.0f * u, tw);
        text(info.name, tx, y, nameSize, withAlpha(theme::kUiText, bodyAlpha));
        y += nameSize + 4.0f * u;

        const float hintSize = 16.0f * u;
        int hintLines = 0;
        if (info.hint[0]) hintLines = font::wrapped(info.hint, 0, 0, tw, hintSize, hintSize * 1.2f, theme::kEvolution, font::Align::Left, 2, false);
        const float hintHeight = static_cast<float>(hintLines) * hintSize * 1.2f;
        const float hintTop = r.y + r.height - pad * 0.6f - hintHeight;
        const float rowHeight = 25.0f * u;
        // The one-line description goes first, but only if at least one stat still fits below it.
        if (info.blurb[0]) {
            const float blurbSize = 16.0f * u;
            const int lines = font::wrapped(info.blurb, tx, y, tw, blurbSize, blurbSize * 1.2f, theme::kUiTextDim, font::Align::Left, 2, false);
            const float need = static_cast<float>(lines) * blurbSize * 1.2f + 4.0f * u;
            const float rowsNeeded = info.rowCount > 0 ? rowHeight : 0.0f;
            if (y + need + rowsNeeded <= hintTop - 2.0f * u) {
                font::wrapped(info.blurb, tx, y, tw, blurbSize, blurbSize * 1.2f, tint(withAlpha(theme::kUiTextDim, bodyAlpha)), font::Align::Left, 2, true);
                y += need;
            }
        }
        for (int i = 0; i < info.rowCount; ++i) {
            if (y + rowHeight > hintTop - 2.0f * u) break;
            const StatRow& row = info.rows[i];
            textMiddle(row.label, tx, y + rowHeight * 0.5f, 18.0f * u, withAlpha(theme::kUiTextDim, bodyAlpha));
            fromTo(tx + tw, y + rowHeight * 0.5f, 20.0f * u, row.from, row.to, row.hasFrom, withAlpha(valueColor, bodyAlpha));
            y += rowHeight;
        }
        if (hintLines > 0) {
            font::wrapped(info.hint, tx, hintTop, tw, hintSize, hintSize * 1.2f, tint(theme::kEvolution), font::Align::Left, 2, true);
        }
    }
}

}  // namespace

void resetLevelUpCards() {
    g_openTime = 0.0f;
    g_nav = Nav{};
}

LevelUpChoice drawLevelUpCards(const GameState& state, float dtReal) {
    LevelUpChoice choice;
    const Layout& l = layout();
    const input::Snapshot& in = input();
    const float u = l.u;
    const int count = std::max(1, state.run.offerCount);
    g_openTime += dtReal;
    const bool ready = g_openTime >= kInputDelaySeconds;
    const float readiness = std::min(1.0f, g_openTime / kInputDelaySeconds);

    DrawRectangle(0, 0, static_cast<int>(l.w), static_cast<int>(l.h), tint(theme::kScreenScrim));

    const Metrics m = computeMetrics(l, count);

    // Heading.
    const float titleSize = (m.columns ? 42.0f : 36.0f) * u;
    textShadowed("LEVEL UP", l.centerX(), m.titleY, titleSize, theme::kUiAccent, font::Align::Center, 4.0f * u);
    char subtitle[96];
    const char* how = in.lastDevice == input::Device::Touch ? "Tap a card to choose" : (in.lastDevice == input::Device::Gamepad ? "D-pad to select, A to choose" : "Arrow keys to select, Enter to choose");
    std::snprintf(subtitle, sizeof(subtitle), "Level %d reached  -  %s", state.player.level, how);
    const float subSize = font::fitSize(subtitle, 18.0f * u, l.width());
    text(subtitle, l.centerX(), m.titleY + titleSize + 4.0f * u, subSize, theme::kUiTextDim, font::Align::Center);
    // A thin line fills while the input delay runs, so the wait looks deliberate.
    if (!ready) {
        const float lineWidth = 160.0f * u;
        const float lineY = m.titleY + titleSize + subSize + 12.0f * u;
        DrawRectangle(static_cast<int>(l.centerX() - lineWidth * 0.5f), static_cast<int>(lineY), static_cast<int>(lineWidth), std::max(2, static_cast<int>(2.0f * u)), tint(theme::kUiBarBack));
        DrawRectangle(static_cast<int>(l.centerX() - lineWidth * 0.5f), static_cast<int>(lineY), static_cast<int>(lineWidth * readiness), std::max(2, static_cast<int>(2.0f * u)), tint(theme::kUiAccent));
    }

    // Navigation. Items 0..count-1 are the cards, and count is the reroll button. With no
    // rerolls left the button is disabled and draws no highlight, so focus must never land
    // on it: it would look as if nothing is selected and Enter did nothing.
    const bool rerollUsable = state.run.rerollsLeft > 0;
    const int items = rerollUsable ? count + 1 : count;
    if (g_nav.selected >= items) g_nav.selected = count - 1;  // The last reroll was just used while it had focus.
    if (ready) {
        // Steps are summed, so two quick presses in one frame move two cards.
        if (m.columns) {
            if (in.stepX != 0 && g_nav.selected < count) g_nav.selected = ((g_nav.selected + in.stepX) % count + count) % count;
            else if (in.stepY > 0 && g_nav.selected < count && rerollUsable) g_nav.selected = count;
            else if (in.stepY < 0 && g_nav.selected == count) g_nav.selected = count / 2;
        } else {
            const int step = in.stepX + in.stepY;
            if (step != 0) g_nav.selected = ((g_nav.selected + step) % items + items) % items;
        }
        if (in.stepX != 0 || in.stepY != 0) audio::playUi(audio::UiSound::Move);
    }

    // Cards.
    const bool showSelection = in.lastDevice != input::Device::Touch;
    for (int i = 0; i < count; ++i) {
        const Card& card = state.run.offers[i];
        CardInfo info;
        describe(state, card, info);
        const Rectangle r = m.cards[i];
        char name[24];
        std::snprintf(name, sizeof(name), "Card %d", i + 1);
        registerRect(name, r);
        if (g_nav.selected == i) markSelected(name);
        if (in.mouseMoved && inside(in.mouse, r) && g_nav.selected != i) {
            g_nav.selected = i;
            audio::playUi(audio::UiSound::Move);
        }
        drawCard(card, info, r, m.columns, showSelection && g_nav.selected == i, readiness);
        if (ready && ((in.pointerPressed && inside(in.pointer, r)) || (in.confirm && g_nav.selected == i))) {
            choice.card = i;
        }
    }

    // Reroll button.
    char rerollLabel[32];
    const int left = state.run.rerollsLeft;
    std::snprintf(rerollLabel, sizeof(rerollLabel), left > 0 ? "Reroll  (%d left)" : "No rerolls left", left);
    const bool wasEnabled = inputEnabled();
    setInputEnabled(ready);
    const bool rerollPressed = button(g_nav, count, m.reroll, rerollLabel, ButtonLook::Normal, promptReroll(), left > 0);
    setInputEnabled(wasEnabled);
    // One action per frame: a card choice wins over a reroll pressed in the same frame.
    // Both together would reroll and then pick from cards the player never saw.
    if (left > 0 && ready && choice.card < 0 && (rerollPressed || in.reroll)) choice.reroll = true;

    // The held build, so the player can see what they own.
    const float weaponSlot = kBuildWeaponSlot * u;
    const float used = drawWeaponSlots(state, m.buildX, m.buildY, weaponSlot);
    const float passivesUsed = drawPassiveSlots(state, m.buildX + used + 18.0f * u, m.buildY + (weaponSlot - kBuildPassiveSlot * u) * 0.5f, kBuildPassiveSlot * u);
    // The strip's real extent, for the layout tests.
    registerExtent("Build strip",Rectangle{m.buildX, m.buildY, used + 18.0f * u + passivesUsed, weaponSlotsHeight(weaponSlot)});

    if (choice.card >= 0) {
        audio::playUi(audio::UiSound::CardPick);
        resetLevelUpCards();
    } else if (choice.reroll) {
        audio::playUi(audio::UiSound::Reroll);
        g_openTime = 0.0f;
    }
    return choice;
}

}  // namespace ls::ui
