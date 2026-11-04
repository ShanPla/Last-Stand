#pragma once

#include "raylib.h"

// Every color in the game lives here. Threats are warm and sharp-edged. The player
// and rewards are cool and round. Enemy bullets get their own hot color, with a
// white core, so they stay readable against everything else.
namespace ls::theme {

inline constexpr Color kBackground{5, 6, 15, 255};
inline constexpr Color kGridLine{26, 42, 84, 255};
inline constexpr Color kGridLineMajor{44, 70, 128, 255};
inline constexpr Color kGridGlow{40, 80, 170, 34};
inline constexpr Color kShadow{0, 0, 0, 150};

// Player and rewards (cool).
inline constexpr Color kPlayer{95, 244, 255, 255};
inline constexpr Color kPlayerFill{10, 40, 56, 255};
inline constexpr Color kPlayerVisor{215, 253, 255, 255};
inline constexpr Color kPlayerPack{40, 120, 170, 255};
inline constexpr Color kPlayerHit{255, 255, 255, 255};
inline constexpr Color kPlayerRing{95, 244, 255, 90};
inline constexpr Color kGunFill{12, 28, 46, 255};
inline constexpr Color kGunPart{150, 215, 255, 255};
inline constexpr Color kMuzzleFlash{150, 240, 255, 255};
inline constexpr Color kMuzzleCore{255, 255, 255, 255};
inline constexpr Color kEvolvedAccent{190, 140, 255, 255};

inline constexpr Color kPlayerShot{120, 240, 255, 255};
inline constexpr Color kShotCore{240, 255, 255, 255};
inline constexpr Color kPellet{170, 215, 255, 255};
inline constexpr Color kFlame{70, 170, 255, 255};
inline constexpr Color kFlameCore{205, 245, 255, 255};
inline constexpr Color kFlameEvolved{165, 110, 255, 255};
inline constexpr Color kGrenade{120, 230, 255, 255};
inline constexpr Color kGrenadeFill{10, 34, 52, 255};
inline constexpr Color kRocketBody{180, 225, 255, 255};
inline constexpr Color kRocketFill{16, 36, 64, 255};
inline constexpr Color kRocketTrail{100, 150, 255, 255};
inline constexpr Color kRocketTrailCore{215, 240, 255, 255};
inline constexpr Color kMine{95, 244, 255, 255};
inline constexpr Color kMineFill{8, 30, 44, 255};
inline constexpr Color kMineLight{235, 255, 255, 255};
inline constexpr Color kBlast{115, 215, 255, 255};
inline constexpr Color kBlastEvolved{185, 135, 255, 255};
inline constexpr Color kBeam{140, 235, 255, 255};
inline constexpr Color kBeamCore{255, 255, 255, 255};
inline constexpr Color kTesla{130, 175, 255, 255};
inline constexpr Color kTeslaCore{240, 247, 255, 255};
inline constexpr Color kBlade{140, 255, 230, 255};
inline constexpr Color kBladeFill{8, 48, 52, 255};
inline constexpr Color kBladeCore{225, 255, 250, 255};

inline constexpr Color kGem{90, 255, 190, 255};
inline constexpr Color kGemFill{8, 48, 44, 255};
inline constexpr Color kGemLarge{120, 190, 255, 255};
inline constexpr Color kGemHuge{190, 165, 255, 255};
inline constexpr Color kGemShine{235, 255, 250, 255};

inline constexpr Color kPickupFill{8, 26, 44, 255};
inline constexpr Color kPickupIcon{240, 252, 255, 255};
inline constexpr Color kPickupHeal{90, 255, 170, 255};
inline constexpr Color kPickupMagnet{105, 165, 255, 255};
inline constexpr Color kPickupBomb{185, 150, 255, 255};
inline constexpr Color kPickupAmmo{110, 235, 255, 255};

// Threats (warm). Each zombie type has its own hue, on top of its own silhouette.
inline constexpr Color kEnemyWalker{255, 96, 72, 255};
inline constexpr Color kEnemyRunner{255, 170, 40, 255};
inline constexpr Color kEnemyBrute{255, 52, 52, 255};
inline constexpr Color kEnemySpitter{255, 92, 176, 255};
inline constexpr Color kEnemyBloater{240, 210, 70, 255};
inline constexpr Color kEnemyCrawler{255, 128, 92, 255};
inline constexpr Color kEnemyCharger{255, 72, 112, 255};
inline constexpr Color kEnemyFill{44, 10, 12, 255};
inline constexpr Color kEnemyEye{255, 240, 150, 255};
inline constexpr Color kEnemyGland{255, 150, 205, 255};
inline constexpr Color kEnemyHorn{255, 214, 130, 255};
inline constexpr Color kEnemyHitFlash{255, 255, 255, 255};
inline constexpr Color kEnemyHitFill{190, 84, 70, 255};

inline constexpr Color kEliteOutline{255, 222, 60, 255};
inline constexpr Color kEliteRing{255, 222, 60, 120};
inline constexpr Color kEliteBarBack{40, 14, 10, 230};
inline constexpr Color kEliteBarFill{255, 222, 60, 255};

inline constexpr Color kChargerFlash{255, 245, 200, 255};
inline constexpr Color kChargerFlashFill{150, 30, 20, 255};
inline constexpr Color kChargerLine{255, 150, 40, 255};

inline constexpr Color kBossPhase1{255, 58, 90, 255};
inline constexpr Color kBossPhase2{255, 110, 40, 255};
inline constexpr Color kBossPhase3{255, 225, 90, 255};
inline constexpr Color kBossFill{50, 8, 16, 255};
inline constexpr Color kBossFillPhase3{78, 14, 12, 255};
inline constexpr Color kBossCore{255, 240, 190, 255};
inline constexpr Color kBossCharge{255, 245, 210, 255};
inline constexpr Color kBossChargeRing{255, 140, 40, 255};

inline constexpr Color kHostileZone{255, 84, 44, 255};
inline constexpr Color kHostileFill{255, 70, 30, 255};
inline constexpr Color kHostileTeeth{255, 195, 60, 255};

inline constexpr Color kEnemyBullet{255, 60, 170, 255};
inline constexpr Color kEnemyBulletCore{255, 245, 248, 255};
inline constexpr Color kEnemyBulletHalo{40, 0, 26, 220};

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
