#include "components/Ui/Names/Names.h"

namespace ls::ui {

namespace {

const char* const kWeaponNames[] = {"Rifle",        "Shotgun",        "SMG",        "Sniper",       "Flamethrower",
                                    "Grenade Launcher", "Homing Rockets", "Landmines", "Tesla Coil", "Combat Blades"};
const char* const kPassiveNames[] = {"Damage", "Fire Rate", "Area", "Move Speed", "Pickup Range", "Armor"};

}  // namespace

const char* weaponName(WeaponId id) {
    const int index = static_cast<int>(id);
    return index >= 0 && index < static_cast<int>(WeaponId::Count) ? kWeaponNames[index] : "";
}

const char* passiveName(PassiveId id) {
    const int index = static_cast<int>(id);
    return index >= 0 && index < static_cast<int>(PassiveId::Count) ? kPassiveNames[index] : "";
}

namespace {
const char* const kDifficultyNames[static_cast<int>(Difficulty::Count)] = {"Easy", "Normal", "Hard", "Hell"};
}  // namespace

const char* difficultyName(Difficulty level) {
    const int index = static_cast<int>(level);
    return index >= 0 && index < static_cast<int>(Difficulty::Count) ? kDifficultyNames[index] : "";
}

const char* const* difficultyNames() { return kDifficultyNames; }

const char* slotName(const WeaponSlot& slot) {
    if (slot.evolved) {
        for (const balance::Evolution& evolution : balance::kEvolutions) {
            if (evolution.weapon == slot.id) return evolution.name;
        }
    }
    return weaponName(slot.id);
}

const balance::Evolution* evolutionForWeapon(WeaponId id) {
    for (const balance::Evolution& evolution : balance::kEvolutions) {
        if (evolution.weapon == id) return &evolution;
    }
    return nullptr;
}

const balance::Evolution* evolutionForPassive(PassiveId id) {
    for (const balance::Evolution& evolution : balance::kEvolutions) {
        if (evolution.passive == id) return &evolution;
    }
    return nullptr;
}

}  // namespace ls::ui
