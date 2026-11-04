#pragma once

#include "raylib.h"

// Every color in the game lives here. Threats are warm and sharp-edged. The player
// and rewards are cool and round. Enemy bullets get their own hot color, with a
// white core, so they stay readable against everything else.
namespace ls::theme {

inline constexpr Color kBackground{5, 6, 15, 255};
inline constexpr Color kGridLine{22, 34, 66, 255};
inline constexpr Color kGridLineMajor{34, 52, 98, 255};

// Player and rewards (cool).
inline constexpr Color kPlayer{95, 244, 255, 255};
inline constexpr Color kPlayerFill{10, 40, 56, 255};
inline constexpr Color kPlayerShot{150, 250, 255, 255};
inline constexpr Color kGem{90, 255, 190, 255};
inline constexpr Color kGemFill{8, 48, 44, 255};
inline constexpr Color kGemLarge{120, 190, 255, 255};

// Threats (warm).
inline constexpr Color kEnemyDrifter{255, 96, 72, 255};
inline constexpr Color kEnemyFill{44, 10, 12, 255};
inline constexpr Color kEnemyHitFlash{255, 255, 255, 255};
inline constexpr Color kEnemyBullet{255, 60, 170, 255};
inline constexpr Color kEnemyBulletCore{255, 240, 245, 255};

// Interface.
inline constexpr Color kText{230, 247, 255, 255};
inline constexpr Color kTextDim{167, 188, 214, 255};
inline constexpr Color kPanel{12, 16, 34, 235};
inline constexpr Color kPanelEdge{70, 110, 170, 255};
inline constexpr Color kAccent{95, 244, 255, 255};
inline constexpr Color kScrim{3, 4, 10, 170};
inline constexpr Color kHealthFill{255, 96, 72, 255};
inline constexpr Color kBarBack{22, 30, 56, 255};
inline constexpr Color kXpFill{90, 255, 190, 255};
inline constexpr Color kDebugText{255, 240, 120, 255};

}  // namespace ls::theme
