#pragma once

#include "raylib.h"

// Every color in the game lives here. Threats are warm and sharp-edged. The player
// and rewards are cool and round. Enemy bullets get their own hot color, with a
// white core, so they stay readable against everything else.
namespace ls::theme {

// --- World -------------------------------------------------------------------
inline constexpr Color kBackground{22, 20, 24, 255};
inline constexpr Color kShadow{8, 6, 14, 120};

// --- Player marker -----------------------------------------------------------
inline constexpr Color kPlayer{120, 220, 255, 255};
inline constexpr Color kPlayerRing{120, 220, 255, 170};
inline constexpr Color kPlayerRingFaint{120, 220, 255, 60};
inline constexpr Color kEvolvedAccent{196, 140, 255, 255};

// --- Shots and blasts: the only things in the world allowed to glow ----------
inline constexpr Color kPlayerShot{255, 226, 130, 255};
inline constexpr Color kShotCore{255, 252, 228, 255};
inline constexpr Color kPellet{255, 196, 110, 255};
inline constexpr Color kFlameEvolvedOuter{104, 70, 230, 255};
inline constexpr Color kFlameEvolvedMiddle{150, 120, 255, 255};
inline constexpr Color kFlameEvolvedCore{226, 214, 255, 255};
inline constexpr Color kSmoke{78, 74, 72, 255};
inline constexpr Color kSmokeLight{120, 114, 108, 255};
inline constexpr Color kBlastWhite{255, 248, 210, 255};
inline constexpr Color kBlastYellow{255, 200, 80, 255};
inline constexpr Color kBlastOrange{240, 120, 36, 255};
inline constexpr Color kBlastRed{150, 44, 24, 255};
inline constexpr Color kBlastEvolvedOuter{120, 80, 240, 255};
inline constexpr Color kGlowWarm{120, 70, 24, 255};
inline constexpr Color kGlowEvolved{70, 40, 130, 255};
inline constexpr Color kBeam{170, 230, 255, 255};
inline constexpr Color kBeamCore{255, 255, 255, 255};
inline constexpr Color kTesla{120, 170, 255, 255};
inline constexpr Color kTeslaCore{236, 244, 255, 255};
inline constexpr Color kBladeTrail{190, 240, 255, 255};

// --- Threat telegraphs: the same warm warning at every time of day ------------
inline constexpr Color kHostileEdge{255, 96, 34, 255};
inline constexpr Color kHostileFill{255, 70, 24, 255};
inline constexpr Color kHostileFront{255, 214, 80, 255};
inline constexpr Color kHostileDark{40, 8, 4, 255};
inline constexpr Color kChargerLine{255, 150, 40, 255};
inline constexpr Color kBossChargeRing{255, 170, 60, 255};
inline constexpr Color kBossSpiralArm{255, 128, 44, 255};  // The spiral warning. Warm like every threat, and not the bullets' pink.
inline constexpr Color kEliteBarBack{24, 10, 8, 235};
inline constexpr Color kEliteBarFill{255, 214, 70, 255};

// --- Sprite palettes ----------------------------------------------------------
// Sprites hold palette positions, not colors. Each material has a ramp, from its
// darkest tone to its lightest. A zombie's outfit is just a row of ramps picked
// from these tables, so crowds vary without any extra texture memory.
struct Ramp4 {
    Color tone[4];
};
struct Ramp3 {
    Color tone[3];
};
struct Ramp2 {
    Color tone[2];
};

inline constexpr Color kNoColor{0, 0, 0, 0};
inline constexpr Color kOutline{20, 16, 22, 255};
inline constexpr Color kFlashHot{255, 196, 90, 255};
inline constexpr Color kSpriteShadow{10, 8, 16, 118};

inline constexpr Ramp4 kZombieSkins[] = {
    {{{52, 82, 50, 255}, {84, 120, 66, 255}, {122, 154, 84, 255}, {166, 190, 116, 255}}},    // Sick green.
    {{{58, 76, 66, 255}, {92, 112, 96, 255}, {130, 148, 124, 255}, {172, 186, 160, 255}}},   // Grey green.
    {{{68, 68, 76, 255}, {104, 104, 110, 255}, {142, 142, 140, 255}, {186, 184, 174, 255}}},  // Ash grey.
    {{{80, 82, 44, 255}, {120, 120, 60, 255}, {160, 156, 84, 255}, {198, 192, 122, 255}}},   // Jaundiced.
};
inline constexpr Ramp2 kZombieRot[] = {
    {{{36, 58, 38, 255}, {58, 44, 44, 255}}},
    {{{42, 52, 48, 255}, {64, 46, 50, 255}}},
    {{{46, 46, 54, 255}, {70, 46, 50, 255}}},
    {{{56, 56, 32, 255}, {76, 50, 40, 255}}},
};
inline constexpr Ramp4 kZombieShirts[] = {
    {{{84, 42, 28, 255}, {132, 66, 38, 255}, {172, 94, 52, 255}, {204, 130, 80, 255}}},      // Rust.
    {{{82, 32, 38, 255}, {128, 50, 52, 255}, {162, 76, 72, 255}, {192, 110, 100, 255}}},     // Faded red.
    {{{100, 78, 24, 255}, {152, 120, 38, 255}, {194, 158, 60, 255}, {224, 194, 100, 255}}},  // Mustard.
    {{{26, 66, 70, 255}, {42, 98, 100, 255}, {68, 132, 126, 255}, {110, 166, 150, 255}}},    // Teal.
    {{{38, 50, 82, 255}, {58, 80, 120, 255}, {88, 112, 150, 255}, {130, 150, 180, 255}}},    // Denim.
    {{{48, 48, 54, 255}, {78, 78, 84, 255}, {108, 106, 110, 255}, {144, 142, 140, 255}}},    // Dirty grey.
    {{{104, 94, 78, 255}, {146, 134, 112, 255}, {184, 172, 146, 255}, {214, 206, 182, 255}}},  // Stained cream.
    {{{64, 34, 66, 255}, {96, 54, 96, 255}, {128, 80, 124, 255}, {160, 116, 150, 255}}},     // Washed purple.
};
inline constexpr Ramp4 kZombiePants[] = {
    {{{26, 36, 66, 255}, {42, 58, 98, 255}, {62, 84, 128, 255}, {94, 118, 158, 255}}},      // Blue jeans.
    {{{78, 66, 44, 255}, {114, 98, 66, 255}, {146, 130, 92, 255}, {174, 160, 120, 255}}},   // Khaki.
    {{{24, 24, 30, 255}, {40, 40, 48, 255}, {58, 58, 68, 255}, {82, 82, 92, 255}}},         // Black.
    {{{50, 34, 26, 255}, {78, 54, 38, 255}, {106, 76, 54, 255}, {134, 102, 74, 255}}},      // Brown.
    {{{40, 56, 44, 255}, {58, 78, 58, 255}, {80, 100, 76, 255}, {106, 124, 98, 255}}},      // Work green.
};
inline constexpr Ramp3 kZombieHair[] = {
    {{{22, 20, 24, 255}, {38, 34, 38, 255}, {60, 54, 56, 255}}},     // Black.
    {{{46, 30, 20, 255}, {72, 48, 30, 255}, {100, 70, 44, 255}}},    // Brown.
    {{{82, 82, 84, 255}, {120, 120, 120, 255}, {156, 156, 152, 255}}},  // Grey.
    {{{96, 44, 20, 255}, {136, 66, 28, 255}, {170, 96, 44, 255}}},   // Ginger.
};
inline constexpr Ramp2 kZombieShoes[] = {
    {{{28, 24, 24, 255}, {52, 46, 42, 255}}},
    {{{50, 36, 26, 255}, {80, 60, 42, 255}}},
    {{{70, 70, 74, 255}, {110, 108, 108, 255}}},
};
inline constexpr Ramp3 kBloodRamp{{{58, 10, 16, 255}, {102, 18, 22, 255}, {146, 32, 28, 255}}};
inline constexpr Color kSocket{18, 12, 16, 255};
inline constexpr Color kEyeGlint{226, 214, 150, 255};
inline constexpr Color kMouthDark{34, 10, 14, 255};
inline constexpr Color kTeeth{216, 208, 170, 255};
inline constexpr Ramp4 kMetalRamp{{{36, 38, 44, 255}, {70, 74, 82, 255}, {110, 114, 122, 255}, {160, 164, 170, 255}}};
inline constexpr Ramp4 kBoneRamp{{{120, 104, 76, 255}, {168, 150, 112, 255}, {206, 192, 150, 255}, {238, 228, 194, 255}}};
inline constexpr Ramp4 kBileRamp{{{70, 100, 20, 255}, {120, 160, 30, 255}, {176, 214, 52, 255}, {224, 246, 120, 255}}};

// Elites keep their figure, and get a strong accent on the outfit and outline.
inline constexpr Color kEliteOutline{255, 206, 60, 255};
inline constexpr Ramp4 kEliteShirt{{{112, 70, 10, 255}, {176, 116, 18, 255}, {226, 170, 40, 255}, {255, 218, 96, 255}}};
inline constexpr Ramp4 kElitePants{{{40, 26, 22, 255}, {64, 42, 30, 255}, {90, 62, 40, 255}, {120, 86, 56, 255}}};

// The boss's three looks. Each phase is hotter than the last.
struct BossPalette {
    Ramp4 skin;
    Ramp4 cloth;
    Ramp4 pants;
    Ramp4 glow;
    Color outline;
};
inline constexpr BossPalette kBossPhases[3] = {
    {{{{50, 66, 48, 255}, {80, 104, 68, 255}, {116, 138, 88, 255}, {156, 172, 118, 255}}},
     {{{70, 40, 40, 255}, {104, 60, 54, 255}, {136, 88, 70, 255}, {166, 118, 92, 255}}},
     {{{34, 38, 56, 255}, {52, 58, 82, 255}, {74, 80, 108, 255}, {102, 106, 134, 255}}},
     {{{120, 20, 20, 255}, {180, 40, 30, 255}, {232, 90, 50, 255}, {255, 170, 110, 255}}},
     {16, 10, 16, 255}},
    {{{{56, 56, 60, 255}, {88, 86, 88, 255}, {122, 118, 114, 255}, {160, 154, 144, 255}}},
     {{{44, 46, 52, 255}, {76, 80, 88, 255}, {116, 120, 128, 255}, {168, 172, 176, 255}}},
     {{{34, 30, 34, 255}, {54, 48, 50, 255}, {78, 70, 70, 255}, {106, 96, 92, 255}}},
     {{{170, 60, 10, 255}, {236, 110, 20, 255}, {255, 170, 40, 255}, {255, 226, 120, 255}}},
     {14, 10, 12, 255}},
    {{{{82, 28, 28, 255}, {128, 44, 40, 255}, {170, 72, 56, 255}, {208, 112, 86, 255}}},
     {{{48, 18, 22, 255}, {78, 28, 30, 255}, {110, 44, 40, 255}, {146, 66, 52, 255}}},
     {{{30, 16, 20, 255}, {50, 24, 28, 255}, {72, 34, 36, 255}, {98, 48, 46, 255}}},
     {{{200, 110, 10, 255}, {255, 180, 30, 255}, {255, 226, 90, 255}, {255, 250, 190, 255}}},
     {24, 6, 8, 255}},
};
inline constexpr Color kBossChargeOutline{255, 226, 120, 255};

// Soldier.
inline constexpr Ramp4 kSoldierHelmet{{{40, 50, 28, 255}, {66, 80, 40, 255}, {96, 112, 56, 255}, {134, 150, 80, 255}}};
inline constexpr Ramp2 kSoldierCamo{{{32, 38, 22, 255}, {112, 98, 58, 255}}};
inline constexpr Ramp4 kSoldierVest{{{50, 48, 30, 255}, {80, 76, 46, 255}, {112, 106, 66, 255}, {146, 140, 92, 255}}};
inline constexpr Ramp4 kSoldierPants{{{42, 50, 30, 255}, {66, 76, 42, 255}, {92, 102, 58, 255}, {120, 130, 78, 255}}};
inline constexpr Ramp3 kSoldierBoots{{{22, 20, 20, 255}, {42, 38, 34, 255}, {70, 64, 54, 255}}};
inline constexpr Ramp2 kSoldierGloves{{{30, 28, 26, 255}, {60, 56, 48, 255}}};
inline constexpr Ramp4 kSoldierFace{{{122, 78, 54, 255}, {174, 120, 86, 255}, {214, 162, 122, 255}, {240, 200, 160, 255}}};
inline constexpr Ramp2 kSoldierStrap{{{28, 28, 24, 255}, {56, 52, 40, 255}}};
inline constexpr Ramp3 kGunRamp{{{26, 28, 32, 255}, {56, 60, 68, 255}, {100, 106, 114, 255}}};
inline constexpr Ramp2 kGunWood{{{70, 44, 26, 255}, {112, 74, 44, 255}}};
inline constexpr Color kGunAccent{70, 180, 220, 255};
inline constexpr Color kGunAccentEvolved{196, 140, 255, 255};
inline constexpr Color kGunTank{196, 76, 40, 255};

// Rewards and small items: round and cool.
inline constexpr Ramp4 kGemSmall{{{20, 96, 110, 255}, {40, 170, 170, 255}, {100, 230, 210, 255}, {214, 255, 244, 255}}};
inline constexpr Ramp4 kGemLarge{{{30, 62, 150, 255}, {60, 110, 220, 255}, {110, 170, 255, 255}, {214, 234, 255, 255}}};
inline constexpr Ramp4 kGemHuge{{{70, 44, 150, 255}, {122, 84, 226, 255}, {172, 140, 255, 255}, {236, 222, 255, 255}}};
inline constexpr Color kBadgeEdge{226, 244, 255, 255};
inline constexpr Color kBadgeShade{120, 170, 200, 255};
inline constexpr Color kBadgeFill{20, 44, 62, 255};
inline constexpr Color kPickupHeal{90, 240, 150, 255};
inline constexpr Color kPickupMagnet{110, 170, 255, 255};
inline constexpr Color kPickupBomb{186, 150, 255, 255};
inline constexpr Color kPickupAmmo{110, 226, 255, 255};
inline constexpr Color kPickupIcon{246, 252, 255, 255};
inline constexpr Ramp3 kCrateWood{{{84, 62, 38, 255}, {132, 100, 60, 255}, {176, 140, 88, 255}}};
inline constexpr Color kBulletBrass{246, 206, 96, 255};
inline constexpr Ramp3 kMineBody{{{26, 30, 34, 255}, {54, 62, 66, 255}, {96, 106, 110, 255}}};
inline constexpr Color kMineLightOn{120, 240, 255, 255};
inline constexpr Color kMineLightOff{36, 80, 96, 255};
inline constexpr Color kMineLightOnEvolved{206, 160, 255, 255};
inline constexpr Ramp3 kGrenadeBody{{{36, 48, 28, 255}, {66, 86, 44, 255}, {108, 130, 70, 255}}};
inline constexpr Ramp3 kRocketBody{{{70, 74, 78, 255}, {140, 146, 150, 255}, {212, 216, 216, 255}}};
inline constexpr Color kRocketNose{216, 70, 44, 255};
inline constexpr Ramp3 kBladeSteel{{{96, 130, 150, 255}, {176, 216, 232, 255}, {255, 255, 255, 255}}};
inline constexpr Ramp3 kBladeSteelEvolved{{{110, 76, 190, 255}, {180, 140, 255, 255}, {255, 244, 255, 255}}};
inline constexpr Ramp3 kMuzzleRamp{{{255, 170, 60, 255}, {255, 226, 130, 255}, {255, 255, 236, 255}}};
inline constexpr Ramp3 kMuzzleRampEvolved{{{150, 96, 255, 255}, {206, 170, 255, 255}, {255, 246, 255, 255}}};

// Enemy bullets: a hot color with a white core and a dark rim, drawn last of all.
inline constexpr Color kEnemyBulletRim{36, 0, 22, 255};
inline constexpr Color kEnemyBullet{255, 44, 130, 255};
inline constexpr Color kEnemyBulletHot{255, 150, 190, 255};
inline constexpr Color kEnemyBulletCore{255, 255, 255, 255};

// --- City map palette --------------------------------------------------------
// Muted and close together on purpose: the ground must always read behind the
// characters, bullets and telegraphs.
inline constexpr Color kAsphalt[3] = {{56, 54, 56, 255}, {62, 60, 62, 255}, {68, 66, 66, 255}};
inline constexpr Color kAsphaltPatch{50, 49, 52, 255};
inline constexpr Color kAsphaltCrack{36, 34, 37, 255};
inline constexpr Color kAsphaltCrackEdge{76, 73, 72, 255};
inline constexpr Color kPothole{32, 30, 33, 255};
inline constexpr Color kPotholeEdge{78, 75, 73, 255};
inline constexpr Color kPaintWhite{140, 136, 124, 255};
inline constexpr Color kPaintYellow{150, 124, 66, 255};
inline constexpr Color kSidewalk[3] = {{94, 90, 84, 255}, {100, 96, 90, 255}, {106, 102, 94, 255}};
inline constexpr Color kSidewalkJoint{72, 69, 66, 255};
inline constexpr Color kCurbLight{122, 118, 108, 255};
inline constexpr Color kCurbShade{44, 42, 44, 255};
inline constexpr Color kDirt[3] = {{84, 72, 58, 255}, {90, 78, 62, 255}, {96, 84, 66, 255}};
inline constexpr Color kDirtDark{68, 58, 48, 255};
inline constexpr Color kGravel{112, 104, 92, 255};
inline constexpr Color kDeadGrass[3] = {{76, 80, 50, 255}, {84, 88, 54, 255}, {94, 94, 58, 255}};
inline constexpr Color kWeed[3] = {{58, 74, 40, 255}, {76, 94, 48, 255}, {98, 112, 58, 255}};
inline constexpr Color kConcreteFloor[3] = {{88, 86, 82, 255}, {94, 92, 86, 255}, {100, 98, 92, 255}};
inline constexpr Color kFloorStain{68, 66, 64, 255};
inline constexpr Color kTileA{96, 100, 98, 255};
inline constexpr Color kTileB{82, 84, 84, 255};
inline constexpr Color kPlank[3] = {{74, 62, 50, 255}, {80, 67, 54, 255}, {86, 72, 58, 255}};
inline constexpr Color kPlankGap{62, 52, 44, 255};
inline constexpr Color kBrick[3] = {{112, 66, 52, 255}, {124, 74, 58, 255}, {136, 84, 64, 255}};
inline constexpr Color kBrickMortar{78, 62, 56, 255};
inline constexpr Color kBrickTop{148, 118, 98, 255};
inline constexpr Color kPlaster[3] = {{132, 122, 106, 255}, {142, 132, 114, 255}, {152, 142, 122, 255}};
inline constexpr Color kPlasterStain{106, 96, 84, 255};
inline constexpr Color kPlasterTop{164, 154, 134, 255};
inline constexpr Color kWallShade{30, 26, 30, 110};
inline constexpr Color kWindowDark{36, 34, 40, 255};
inline constexpr Color kBeamWood[3] = {{52, 38, 28, 255}, {70, 52, 36, 255}, {90, 68, 46, 255}};
inline constexpr Color kRebar{82, 58, 48, 255};
inline constexpr Color kRubble[4] = {{80, 72, 66, 255}, {104, 94, 84, 255}, {126, 90, 72, 255}, {138, 128, 112, 255}};
inline constexpr Color kPropShadow{20, 18, 24, 96};
inline constexpr Color kRust[3] = {{82, 44, 28, 255}, {112, 62, 36, 255}, {142, 84, 48, 255}};
inline constexpr Color kBarrelBand{54, 40, 32, 255};
inline constexpr Color kMetalProp[3] = {{62, 64, 68, 255}, {92, 94, 96, 255}, {122, 124, 122, 255}};
inline constexpr Color kFenceMesh{118, 118, 112, 150};
inline constexpr Color kFencePost{92, 94, 92, 255};
inline constexpr Color kSignRed{130, 52, 44, 255};
inline constexpr Color kSignRedDark{92, 38, 34, 255};
inline constexpr Color kSignWhite{164, 158, 144, 255};
inline constexpr Color kSignBlue{58, 78, 106, 255};
inline constexpr Color kLampHead{120, 120, 112, 255};
inline constexpr Color kLampGlass{170, 162, 120, 255};
inline constexpr Color kCarPaints[4][3] = {
    {{70, 46, 38, 255}, {98, 62, 46, 255}, {124, 82, 58, 255}},
    {{54, 62, 72, 255}, {76, 86, 96, 255}, {100, 110, 118, 255}},
    {{84, 76, 50, 255}, {112, 102, 66, 255}, {138, 126, 84, 255}},
    {{74, 42, 44, 255}, {102, 56, 54, 255}, {128, 76, 68, 255}},
};
inline constexpr Color kCarGlass{40, 46, 54, 255};
inline constexpr Color kCarGlassShine{80, 92, 100, 255};
inline constexpr Color kTire{28, 26, 28, 255};
inline constexpr Color kTentFabric[3][3] = {
    {{62, 72, 56, 255}, {84, 94, 70, 255}, {106, 114, 86, 255}},
    {{86, 72, 50, 255}, {112, 96, 66, 255}, {136, 118, 84, 255}},
    {{56, 66, 84, 255}, {76, 88, 106, 255}, {98, 110, 124, 255}},
};
inline constexpr Color kTentDark{30, 28, 30, 255};
inline constexpr Color kAsh{58, 54, 52, 255};
inline constexpr Color kCoal{32, 26, 26, 255};
inline constexpr Color kSandbag[3] = {{98, 88, 66, 255}, {118, 108, 82, 255}, {138, 128, 98, 255}};
inline constexpr Color kCloth[3] = {{92, 60, 54, 255}, {62, 76, 88, 255}, {98, 92, 66, 255}};
inline constexpr Color kPuddle{50, 58, 68, 255};
inline constexpr Color kPuddleShine{78, 90, 100, 255};
inline constexpr Color kTreeBark[3] = {{44, 34, 28, 255}, {62, 48, 38, 255}, {82, 66, 50, 255}};

// Small live lights on the map.
inline constexpr Color kLampLight{230, 196, 120, 255};  // How much a light brightens the ground at its center.
inline constexpr Color kFireLight{240, 130, 60, 255};
inline constexpr Color kPlayerLight{120, 130, 150, 255};  // His own small pool of light at night.
inline constexpr Color kFireFlame[3] = {{226, 90, 30, 255}, {250, 160, 50, 255}, {255, 232, 150, 255}};

// --- Wasteland map -------------------------------------------------------------
// Sand is kept mid-dark, and every ground tone sits close to its neighbors, so that the
// characters, gems, bullets and telegraphs always read first. Only thin lines such as
// cracks and dune crests are allowed some contrast: they show that the player is moving.
inline constexpr Color kSand[4] = {{97, 88, 70, 255}, {102, 92, 73, 255}, {107, 97, 77, 255}, {112, 101, 80, 255}};
inline constexpr Color kSandLee{84, 76, 63, 255};
inline constexpr Color kSandShade{92, 84, 68, 255};
inline constexpr Color kSandCrest{120, 109, 87, 255};
inline constexpr Color kPan[3] = {{96, 90, 78, 255}, {100, 94, 82, 255}, {105, 98, 85, 255}};
inline constexpr Color kPanCrack{64, 57, 50, 255};
inline constexpr Color kPanCrackLit{114, 106, 92, 255};
inline constexpr Color kPanCrackFaint{82, 74, 62, 255};
inline constexpr Color kReg[3] = {{80, 74, 64, 255}, {85, 79, 68, 255}, {90, 83, 71, 255}};
inline constexpr Color kRedEarth[3] = {{94, 75, 60, 255}, {99, 79, 63, 255}, {104, 83, 66, 255}};
inline constexpr Color kScrubEarth[3] = {{90, 85, 66, 255}, {95, 89, 69, 255}, {100, 93, 72, 255}};
inline constexpr Color kStone[4] = {{58, 53, 48, 255}, {80, 73, 64, 255}, {102, 93, 80, 255}, {122, 113, 96, 255}};
inline constexpr Color kDryGrass[3] = {{76, 68, 42, 255}, {98, 86, 48, 255}, {120, 106, 60, 255}};
inline constexpr Color kTwig{54, 44, 34, 255};
inline constexpr Color kTrackDark{30, 24, 22, 64};
inline constexpr Color kTrackLit{150, 136, 108, 48};

inline constexpr Color kOldAsphalt[3] = {{63, 60, 58, 255}, {68, 64, 62, 255}, {73, 69, 66, 255}};
inline constexpr Color kOldAsphaltCrack{40, 38, 38, 255};
inline constexpr Color kOldAsphaltLit{86, 81, 75, 255};
inline constexpr Color kTarSnake{45, 43, 44, 255};
inline constexpr Color kOldAsphaltPatch{57, 55, 54, 255};
inline constexpr Color kFadedYellow{128, 108, 64, 255};
inline constexpr Color kFadedWhite{122, 117, 102, 255};
inline constexpr Color kSkidMark{30, 28, 30, 110};

inline constexpr Color kWreckPaints[5][3] = {
    {{52, 74, 76, 255}, {74, 100, 98, 255}, {100, 124, 116, 255}},   // Sun-bleached teal.
    {{80, 50, 42, 255}, {106, 68, 54, 255}, {130, 92, 72, 255}},     // Old red.
    {{92, 76, 44, 255}, {126, 106, 62, 255}, {150, 130, 84, 255}},   // Dull mustard.
    {{64, 68, 76, 255}, {90, 94, 100, 255}, {118, 120, 122, 255}},   // Chalky blue grey.
    {{78, 66, 54, 255}, {108, 94, 76, 255}, {136, 122, 100, 255}},   // Faded cream.
};
inline constexpr Color kWreckBurnt[3] = {{30, 26, 26, 255}, {46, 40, 38, 255}, {66, 56, 50, 255}};
inline constexpr Color kBusPaint[3] = {{86, 70, 38, 255}, {112, 92, 50, 255}, {132, 112, 68, 255}};
inline constexpr Color kTankerPaint[3] = {{74, 76, 78, 255}, {104, 106, 106, 255}, {134, 134, 130, 255}};
// Bits left scattered around a flattened car: broken glass, bent metal and rust flakes.
inline constexpr Color kFlatCarBits[3] = {{150, 168, 174, 255}, {78, 80, 84, 255}, {116, 66, 40, 255}};
inline constexpr Color kDustyGlass{42, 46, 50, 255};
inline constexpr Color kDustyGlassShine{84, 92, 94, 255};

inline constexpr Color kRock[4] = {{50, 46, 46, 255}, {72, 66, 62, 255}, {94, 86, 78, 255}, {118, 110, 98, 255}};
inline constexpr Color kRockRed[4] = {{58, 42, 38, 255}, {84, 60, 50, 255}, {106, 78, 62, 255}, {128, 98, 78, 255}};
inline constexpr Color kTireInner{50, 46, 46, 255};
inline constexpr Color kTireTop{62, 58, 56, 255};
inline constexpr Color kBone[3] = {{104, 94, 74, 255}, {136, 126, 102, 255}, {160, 150, 124, 255}};
inline constexpr Color kBoneHole{40, 34, 30, 255};
inline constexpr Color kDeadWood[3] = {{42, 34, 28, 255}, {62, 50, 40, 255}, {84, 70, 54, 255}};
inline constexpr Color kWirePost{66, 54, 42, 255};
inline constexpr Color kWireStrand{36, 34, 34, 170};
inline constexpr Color kWireBarb{26, 24, 24, 255};
inline constexpr Color kPoleWood[2] = {{46, 36, 30, 255}, {68, 54, 42, 255}};
inline constexpr Color kPowerWire{24, 22, 24, 120};
inline constexpr Color kRailMetal[3] = {{60, 58, 56, 255}, {88, 86, 80, 255}, {112, 108, 98, 255}};
inline constexpr Color kMarkerAmber{150, 104, 44, 255};
inline constexpr Color kSignGreen{46, 72, 58, 255};
inline constexpr Color kSignYellow{138, 112, 52, 255};
inline constexpr Color kSignInk{36, 32, 30, 255};
inline constexpr Color kSignPale{136, 130, 112, 255};
inline constexpr Color kBillboard[3] = {{92, 62, 48, 255}, {118, 104, 80, 255}, {62, 78, 84, 255}};

inline constexpr Color kCorrugated[3] = {{68, 48, 38, 255}, {88, 64, 50, 255}, {110, 84, 66, 255}};
inline constexpr Color kCorrugatedGrey[3] = {{62, 62, 62, 255}, {84, 84, 82, 255}, {106, 104, 98, 255}};
inline constexpr Color kAdobe[3] = {{100, 84, 64, 255}, {112, 95, 72, 255}, {124, 106, 82, 255}};
inline constexpr Color kAdobeTop{136, 120, 94, 255};
inline constexpr Color kCinder[3] = {{84, 82, 76, 255}, {96, 94, 86, 255}, {108, 105, 96, 255}};
inline constexpr Color kCinderTop{124, 120, 108, 255};
inline constexpr Color kApron[3] = {{84, 81, 74, 255}, {89, 86, 78, 255}, {94, 91, 82, 255}};
inline constexpr Color kApronJoint{62, 59, 55, 255};
inline constexpr Color kOilStain{24, 22, 26, 90};
inline constexpr Color kFloorDark{60, 54, 48, 255};
inline constexpr Color kPumpBody[3] = {{70, 44, 38, 255}, {108, 66, 52, 255}, {136, 94, 74, 255}};
inline constexpr Color kPumpFace{38, 40, 42, 255};
inline constexpr Color kCanopyStripe[2] = {{104, 56, 44, 255}, {122, 116, 100, 255}};

// --- Day cycle ---------------------------------------------------------------
// The run moves from golden afternoon, through dusk and night, to a blood-red sky
// for the boss. The ground is graded strongly, characters only gently, so threats
// stay readable at the darkest moment. Colors are multipliers, shown as 0-255 of
// a range from 0 to 1.25, so that a warm light can also brighten.
struct DayKey {
    float seconds;
    Color groundTint;
    float groundSaturation;
    Color groundLift;
    Color spriteTint;
    float lampLevel;      // 0 means off, 1 means fully on.
    float shadowLength;   // Sun shadow length, in figure heights.
    float vignette;
};
inline constexpr DayKey kDayKeys[] = {
    //  time  ground tint             sat    ground lift       sprite tint           lamp   shadow  vignette
    {0.0f, {255, 216, 160, 255}, 1.05f, {12, 6, 0, 255}, {255, 240, 214, 255}, 0.00f, 1.30f, 0.20f},   // Golden afternoon.
    {120.0f, {250, 186, 140, 255}, 1.00f, {12, 4, 2, 255}, {255, 228, 204, 255}, 0.00f, 1.70f, 0.26f},  // Low sun.
    {210.0f, {186, 136, 126, 255}, 0.85f, {8, 3, 8, 255}, {244, 216, 206, 255}, 0.10f, 1.60f, 0.34f},  // Dusk.
    {300.0f, {116, 102, 142, 255}, 0.70f, {4, 2, 12, 255}, {214, 202, 218, 255}, 0.55f, 0.80f, 0.42f},  // Late dusk.
    {370.0f, {74, 84, 134, 255}, 0.55f, {2, 4, 14, 255}, {176, 184, 218, 255}, 1.00f, 0.00f, 0.50f},   // Night.
    {500.0f, {56, 64, 112, 255}, 0.50f, {2, 2, 12, 255}, {176, 182, 220, 255}, 1.00f, 0.00f, 0.55f},   // Deep night.
    {590.0f, {110, 38, 46, 255}, 0.60f, {10, 0, 2, 255}, {240, 192, 186, 255}, 1.00f, 0.00f, 0.60f},   // Blood-red sky.
    {600.0f, {122, 36, 42, 255}, 0.60f, {12, 0, 2, 255}, {244, 192, 186, 255}, 1.00f, 0.00f, 0.60f},
};
inline constexpr float kDayTintRange = 1.25f;
inline constexpr float kDuskSeconds = 240.0f;  // Used for the title screen demo.

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
inline constexpr Color kDebugCarBox{120, 255, 120, 255};

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

// --- Effects ---
// Gore stays dark and muted, so it never competes with enemy bullets or telegraphs.
// Only sparks and glows are bright, because shots and blasts are the one neon layer.
inline constexpr Color kFxBlood[3] = {{52, 10, 14, 255}, {84, 16, 20, 255}, {120, 26, 28, 255}};
inline constexpr Color kFxGore[3] = {{38, 18, 20, 255}, {62, 30, 28, 255}, {122, 108, 86, 255}};  // The last is a bone chip.
inline constexpr Color kFxBile[3] = {{48, 66, 18, 255}, {78, 102, 26, 255}, {112, 136, 38, 255}};
inline constexpr Color kFxPlayerBlood[3] = {{92, 18, 20, 255}, {128, 26, 26, 255}, {160, 40, 34, 255}};
inline constexpr Color kFxSpark[3] = {{255, 248, 214, 255}, {255, 204, 110, 255}, {226, 124, 46, 255}};
inline constexpr Color kFxTeslaSpark[3] = {{236, 244, 255, 255}, {170, 205, 255, 255}, {110, 150, 240, 255}};
inline constexpr Color kFxEmber[3] = {{255, 196, 90, 255}, {240, 130, 44, 255}, {168, 66, 28, 255}};
inline constexpr Color kFxEvolvedSpark[3] = {{246, 236, 255, 255}, {196, 140, 255, 255}, {120, 80, 220, 255}};
inline constexpr Color kFxSmoke[2] = {{84, 80, 78, 255}, {112, 106, 100, 255}};
inline constexpr Color kFxDebris[3] = {{52, 46, 44, 255}, {78, 70, 64, 255}, {104, 94, 84, 255}};
inline constexpr Color kFxDust[2] = {{92, 84, 72, 255}, {122, 112, 96, 255}};
// What flies when the boss flattens a car: pale road dust, and bent metal, rust and glass.
inline constexpr Color kFxCrushDust[3] = {{96, 88, 78, 255}, {128, 118, 102, 255}, {158, 148, 128, 255}};
inline constexpr Color kFxCrushMetal[3] = {{84, 86, 90, 255}, {128, 74, 44, 255}, {170, 186, 190, 255}};
inline constexpr Color kFxBrass{226, 186, 84, 255};
inline constexpr Color kFxGemSparkle[3] = {{214, 255, 244, 255}, {214, 234, 255, 255}, {236, 222, 255, 255}};
inline constexpr Color kFxLevelUp[3] = {{255, 240, 170, 255}, {255, 206, 96, 255}, {150, 230, 214, 255}};
inline constexpr Color kFxBossBurst[3][3] = {
    {{255, 170, 110, 255}, {232, 90, 50, 255}, {120, 30, 24, 255}},
    {{255, 226, 120, 255}, {255, 170, 40, 255}, {170, 60, 10, 255}},
    {{255, 250, 190, 255}, {255, 180, 30, 255}, {200, 70, 10, 255}},
};
// Ground stains. These fade through alpha, so they keep their dark color to the end.
inline constexpr Color kFxStainBlood{40, 12, 16, 255};
inline constexpr Color kFxStainBile{38, 50, 18, 255};
inline constexpr Color kFxStainScorch{20, 18, 22, 255};
// Damage numbers. They stay in soft cream and gold, away from the pink of enemy bullets.
inline constexpr Color kFxNumberSmall{222, 214, 196, 255};
inline constexpr Color kFxNumberBig{255, 208, 92, 255};
inline constexpr Color kFxNumberHuge{255, 142, 62, 255};
inline constexpr Color kFxNumberEdge{18, 12, 16, 255};
inline constexpr Color kFxNumberMask{255, 255, 255, 255};  // White in the digit texture, which each number then tints.
// Glows are added on top of the picture, so a low value is already bright.
inline constexpr Color kFxGlowShot{255, 190, 96, 255};
inline constexpr Color kFxGlowEvolved{170, 110, 255, 255};
inline constexpr Color kFxGlowMuzzle{255, 206, 120, 255};
inline constexpr Color kFxGlowBlast{255, 136, 48, 255};
inline constexpr Color kFxGlowBlastCore{255, 226, 170, 255};
inline constexpr Color kFxGlowBeam{130, 200, 255, 255};
inline constexpr Color kFxGlowTesla{100, 150, 255, 255};
inline constexpr Color kFxGlowFlame{255, 120, 40, 255};
// What a hit figure mixes toward. A little warm, so a hit reads as light on the figure.
inline constexpr Color kFxHitFlash{255, 244, 226, 255};
// Full-screen flashes. They are always faint, and only ever drawn through the limiter.
inline constexpr Color kFxScreenFlashWarm{255, 228, 180, 255};
inline constexpr Color kFxScreenFlashHostile{255, 110, 70, 255};
inline constexpr Color kFxScreenFlashEvolved{206, 164, 255, 255};

// --- Dash button and cooldown (HUD) ---
inline constexpr Color kDashReady{255, 196, 64, 255};        // Border and icon when a dash is ready.
inline constexpr Color kDashCooling{112, 102, 96, 255};      // Dim icon while it recharges.
inline constexpr Color kDashShade{14, 12, 16, 190};          // Covers the part of the button not yet recharged.
inline constexpr Color kDashActive{255, 236, 170, 255};      // The button while the dash itself runs.
inline constexpr Color kDashButtonFill{40, 34, 30, 255};

// --- Difficulty labels (title, pause, game over) ---
inline constexpr Color kDifficultyEasy{112, 208, 170, 255};
inline constexpr Color kDifficultyNormal{240, 226, 196, 255};
inline constexpr Color kDifficultyHard{222, 92, 60, 255};
inline constexpr Color kDifficultyHell{244, 58, 104, 255};
inline constexpr Color kDifficultyColors[4] = {kDifficultyEasy, kDifficultyNormal, kDifficultyHard, kDifficultyHell};  // Indexed by Difficulty.

// --- Map selector ---
inline constexpr Color kMapColors[2] = {Color{176, 170, 190, 255}, Color{226, 178, 104, 255}};

}  // namespace ls::theme
