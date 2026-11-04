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

// --- Screens ---
// Menus, HUD and overlays: a muted dusk palette of dark slate, sand and rust, with
// one strong gold accent for selection. Warm red and orange mark danger, cool teal and
// blue mark the player and rewards, and dusk violet marks evolutions. Screens use only
// the colors in this section, so the rest of the theme can change without touching them.
inline constexpr Color kUiText{240, 226, 196, 255};
inline constexpr Color kUiTextDim{178, 162, 140, 255};
inline constexpr Color kUiPanel{40, 36, 44, 255};
inline constexpr Color kUiBorder{128, 108, 92, 255};        // The light tone of a two-tone border.
inline constexpr Color kUiBorderDark{14, 12, 16, 255};      // The dark tone, on the outside.
inline constexpr Color kUiAccent{255, 196, 64, 255};        // Selection and primary buttons.
inline constexpr Color kUiAccentDark{150, 96, 24, 255};
inline constexpr Color kUiBarBack{26, 24, 30, 255};
inline constexpr Color kUiHealth{206, 70, 52, 255};
inline constexpr Color kUiHealthBack{64, 26, 26, 255};
inline constexpr Color kUiXp{96, 204, 176, 255};
inline constexpr Color kUiGem{112, 208, 170, 255};          // Rewards and passive icons.
inline constexpr Color kUiGemBlue{132, 176, 224, 255};
inline constexpr Color kUiWeapon{150, 200, 224, 255};       // Weapon icons and the player.
inline constexpr Color kUiWeaponFill{28, 44, 56, 255};
inline constexpr Color kUiWeaponLight{208, 236, 246, 255};
inline constexpr Color kLogoRust{214, 92, 60, 255};
inline constexpr Color kLogoShadow{112, 36, 26, 255};
inline constexpr Color kZombieSkin{132, 164, 100, 255};
inline constexpr Color kZombieDark{58, 82, 50, 255};

inline constexpr Color kScreenScrim{10, 10, 14, 205};       // Behind pause, settings and game over.
inline constexpr Color kTitleShade{12, 10, 14, 235};        // Dark side of the title.
inline constexpr Color kHudPlate{16, 14, 20, 170};          // Translucent plate under HUD icons.
inline constexpr Color kButtonFill{48, 42, 50, 255};
inline constexpr Color kButtonHover{74, 62, 58, 255};
inline constexpr Color kButtonPrimaryHover{255, 224, 128, 255};
inline constexpr Color kButtonDisabled{30, 28, 34, 255};
inline constexpr Color kTextOnPrimary{28, 20, 12, 255};     // Dark text on a gold button.
inline constexpr Color kTextDisabled{112, 102, 96, 255};
inline constexpr Color kDivider{78, 66, 64, 255};
inline constexpr Color kUiShadow{0, 0, 0, 190};
inline constexpr Color kKeyChipFill{22, 20, 26, 255};
inline constexpr Color kDefeat{222, 92, 60, 255};           // Game over headline.
inline constexpr Color kVictory{128, 214, 150, 255};
inline constexpr Color kNewBest{112, 208, 170, 255};
inline constexpr Color kBossBar{222, 76, 60, 255};
inline constexpr Color kBossBarBack{58, 22, 24, 255};
inline constexpr Color kLowHealth{200, 44, 32, 255};        // Edge pulse when health is low.
inline constexpr Color kEmptyMark{240, 132, 40, 255};       // The EMPTY tag on a dry weapon.
inline constexpr Color kAmmoFill{120, 196, 214, 255};
inline constexpr Color kReload{160, 176, 232, 255};
inline constexpr Color kEvolution{184, 140, 224, 255};
inline constexpr Color kEvolutionFill{46, 34, 62, 255};
inline constexpr Color kIconDim{96, 96, 104, 255};
inline constexpr Color kJoystick{150, 200, 224, 255};
inline constexpr Color kFadeFromDark{10, 10, 14, 255};      // Covers the screen for a moment when a run starts.
// Bars on the game over screen, sorted from most to least damage.
inline constexpr Color kBarPalette[6] = {{150, 200, 224, 255}, {132, 176, 224, 255}, {112, 208, 170, 255},
                                         {184, 140, 224, 255}, {240, 196, 120, 255}, {160, 214, 196, 255}};

}  // namespace ls::theme
