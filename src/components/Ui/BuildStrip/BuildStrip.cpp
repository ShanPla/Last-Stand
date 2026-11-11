#include "components/Ui/BuildStrip/BuildStrip.h"

#include "components/Ui/Icons/Icons.h"
#include "components/Ui/Widgets/Widgets.h"
#include "components/Weapons/Weapons.h"
#include "data/Theme.h"

#include <algorithm>
#include <cstdio>

namespace ls::ui {

namespace {

constexpr float kBarHeightShare = 0.13f;

}  // namespace

float weaponSlotsHeight(float slot) { return slot + slot * kBarHeightShare + slot * 0.08f; }

float drawWeaponSlots(const GameState& state, float x, float y, float slot) {
    const Player& player = state.firstPlayer();
    const float gap = slot * kSlotGap;
    char label[8];
    for (int i = 0; i < player.weaponCount; ++i) {
        const WeaponSlot& weapon = player.weapons[i];
        const balance::WeaponLevel stats = weapons::effectiveStats(state.firstPlayer(), weapon);
        const bool usesAmmo = stats.magazine > 0;
        const bool reloading = weapon.reloadLeft > 0.0f;
        const bool dry = usesAmmo && weapon.magazine <= 0 && weapon.reserve <= 0 && !reloading;
        const float left = x + static_cast<float>(i) * (slot + gap);
        const Rectangle plate{left, y, slot, slot};

        roundedRect(plate, slot * 0.2f, theme::kHudPlate);
        roundedOutline(plate, slot * 0.2f, weapon.evolved ? 2.2f * u() : 1.2f * u(), weapon.evolved ? theme::kEvolution : withAlpha(theme::kUiBorder, 0.7f));
        drawWeaponIcon(weapon.id, left + slot * 0.5f, y + slot * 0.5f, slot * 0.62f, tint(dry ? theme::kIconDim : theme::kUiWeapon));

        std::snprintf(label, sizeof(label), "%d", weapon.level);
        textShadowed(label, left + slot - slot * 0.1f, y + slot * 0.04f, slot * 0.3f, weapon.evolved ? theme::kEvolution : theme::kUiText, font::Align::Right);

        if (usesAmmo) {
            const float barHeight = std::max(3.0f, slot * kBarHeightShare);
            const Rectangle barRect{left, y + slot + slot * 0.08f, slot, barHeight};
            if (reloading) {
                const float progress = stats.reload > 0.0f ? 1.0f - weapon.reloadLeft / stats.reload : 0.0f;
                bar(barRect, progress, theme::kReload, theme::kUiBarBack);
            } else {
                const float fill = static_cast<float>(weapon.magazine) / static_cast<float>(std::max(1, stats.magazine));
                bar(barRect, fill, dry ? theme::kEmptyMark : theme::kAmmoFill, theme::kUiBarBack);
            }
        }
        if (dry) {
            const float tagHeight = slot * 0.3f;
            const Rectangle tag{left + slot * 0.06f, y + slot * 0.62f, slot * 0.88f, tagHeight};
            roundedRect(tag, tagHeight * 0.3f, theme::kEmptyMark);
            textMiddle("EMPTY", tag.x + tag.width * 0.5f, tag.y + tagHeight * 0.5f, tagHeight * 0.74f, theme::kTextOnPrimary, font::Align::Center);
        }
    }
    return player.weaponCount > 0 ? static_cast<float>(player.weaponCount) * (slot + gap) - gap : 0.0f;
}

float drawPassiveSlots(const GameState& state, float x, float y, float slot) {
    const Player& player = state.firstPlayer();
    const float gap = slot * kSlotGap;
    char label[8];
    for (int i = 0; i < player.passiveCount; ++i) {
        const PassiveSlot& passive = player.passives[i];
        const float left = x + static_cast<float>(i) * (slot + gap);
        const Rectangle plate{left, y, slot, slot};
        roundedRect(plate, slot * 0.2f, theme::kHudPlate);
        roundedOutline(plate, slot * 0.2f, 1.2f * u(), withAlpha(theme::kUiBorder, 0.7f));
        drawPassiveIcon(passive.id, left + slot * 0.5f, y + slot * 0.5f, slot * 0.6f, tint(theme::kUiGem));
        std::snprintf(label, sizeof(label), "%d", passive.level);
        textShadowed(label, left + slot - slot * 0.1f, y + slot * 0.02f, slot * 0.38f, theme::kUiText, font::Align::Right);
    }
    return player.passiveCount > 0 ? static_cast<float>(player.passiveCount) * (slot + gap) - gap : 0.0f;
}

}  // namespace ls::ui
