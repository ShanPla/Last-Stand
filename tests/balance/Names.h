#pragma once

#include "core/GameState.h"

namespace ls::tools {

inline constexpr int kWeaponCount = static_cast<int>(WeaponId::Count);

inline const char* weaponName(int index) {
    static const char* const names[kWeaponCount] = {
        "Rifle", "Shotgun", "SMG", "Sniper", "Flamethrower", "Grenade", "Rockets", "Landmines", "Tesla", "Blades",
    };
    return names[index];
}

inline const char* passiveName(PassiveId id) {
    static const char* const names[static_cast<int>(PassiveId::Count)] = {
        "Damage", "FireRate", "Area", "MoveSpeed", "PickupRange", "Armor",
    };
    return names[static_cast<int>(id)];
}

inline const char* difficultyName(Difficulty difficulty) {
    static const char* const names[static_cast<int>(Difficulty::Count)] = {"easy", "normal", "hard", "hell"};
    return names[static_cast<int>(difficulty)];
}

}  // namespace ls::tools
