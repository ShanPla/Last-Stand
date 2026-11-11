#pragma once

#include "components/WorldView/Canvas.h"
#include "core/GameState.h"

#include <cstdint>

// The sprite atlas: one texture, built in code at startup from layered body parts.
// It stores palette positions, not colors. A fragment shader looks each one up in a
// palette texture, so one figure can wear any outfit with no extra memory.
namespace ls::worldview {

// Columns of the palette texture. Every sprite shares this one layout.
enum PaletteColumn : uint8_t {
    kPClear = 0,
    kPOutline = 1,
    kPSkin = 2,    // 4 tones
    kPRot = 6,     // 2 tones
    kPHair = 8,    // 3 tones
    kPShirt = 11,  // 4 tones
    kPPants = 15,  // 4 tones
    kPShoes = 19,  // 2 tones
    kPBlood = 21,  // 3 tones
    kPSocket = 24,
    kPGlint = 25,
    kPMouth = 26,
    kPTeeth = 27,
    kPMetal = 28,  // 4 tones
    kPBone = 32,   // 4 tones
    kPBile = 36,   // 4 tones
    kPHelmet = 40,  // 4 tones
    kPCamo = 44,    // 2 tones
    kPVest = 46,    // 4 tones
    kPSoldierPants = 50,  // 4 tones
    kPBoots = 54,   // 3 tones
    kPGloves = 57,  // 2 tones
    kPFace = 59,    // 4 tones
    kPStrap = 63,   // 2 tones
    kPGun = 65,     // 3 tones
    kPWood = 68,    // 2 tones
    kPGunAccent = 70,
    kPGunTank = 71,
    kPGemSmall = 72,  // 4 tones
    kPGemLarge = 76,  // 4 tones
    kPGemHuge = 80,   // 4 tones
    kPBadgeEdge = 84,
    kPBadgeShade = 85,
    kPBadgeFill = 86,
    kPHeal = 87,
    kPMagnet = 88,
    kPBomb = 89,
    kPAmmo = 90,
    kPIcon = 91,
    kPCrate = 92,  // 3 tones
    kPBrass = 95,
    kPMine = 96,   // 3 tones
    kPMineOn = 99,
    kPMineOff = 100,
    kPGrenade = 101,  // 3 tones
    kPRocket = 104,   // 3 tones
    kPRocketNose = 107,
    kPBlade = 108,    // 3 tones
    kPMuzzle = 111,   // 3 tones
    kPEnemyRim = 114,
    kPEnemyHot = 115,
    kPEnemyHotLight = 116,
    kPEnemyCore = 117,
    kPFlame = 118,  // 3 tones
    kPLight = 121,  // 3 tones
    kPGrowth = 124, // 4 tones. Boils and tumors, which only the boss has.
    kPaletteWidth = 128,
};

// Rows of the palette texture. A sprite picks its row in the vertex color.
enum PaletteRow : uint8_t {
    kRowOutfits = 0,
    kOutfitCount = 32,
    kRowElite = 32,  // One per zombie type.
    kRowShadow = 41,
    kRowBase = 42,
    kRowEvolved = 43,
    kRowHot = 44,    // Charger and boss telegraph flash.
    kRowBoss = 45,   // Three rows, one per phase, for the difficulty the atlas holds now.
    kRowBossCharge = 48,
    kRowFireHot = 49,   // These recolor the same puff sprite as fire or smoke.
    kRowFireOld = 50,
    kRowSmoke = 51,
    kRowSmokeLight = 52,
    kRowLampLight = 53,  // Pools of light, multiplied onto the ground.
    kRowFireLight = 54,
    kRowPlayerLight = 55,
    kRowTeam = 56,   // Co-op: four rows, one soldier color per player.
    kTeamRows = 4,
    kPaletteHeight = 64,
};

struct SpriteRef {
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t w = 0;
    uint8_t h = 0;
    uint8_t footX = 0;  // Where the anchor (usually the feet) is inside the sprite.
    uint8_t footY = 0;
};

enum class Figure : uint8_t { Walker, Runner, Brute, Spitter, Bloater, Crawler, Charger, Boss1, Boss2, Boss3, Count };
inline constexpr int kFigureCount = static_cast<int>(Figure::Count);

enum Facing : uint8_t { kFacingSide, kFacingFront, kFacingBack, kFacingCount };

// A figure's poses. The first ones are the walk cycle, the rest are special.
inline constexpr int kWalkFrames = 6;
inline constexpr int kBossWalkFrames = 4;
enum Pose : uint8_t {
    kPoseSpecial = kWalkFrames,
    kPoseSpitInflate = kPoseSpecial,
    kPoseSpitRecoil = kPoseSpecial + 1,
    kPoseBraceA = kPoseSpecial,
    kPoseBraceB = kPoseSpecial + 1,
    kPoseDashA = kPoseSpecial + 2,
    kPoseDashB = kPoseSpecial + 3,
    kPoseRecover = kPoseSpecial + 4,
    kPoseCount = kPoseSpecial + 5,
};

// Pre-rendered sizes. Perspective picks the nearest one. Each was painted at its own
// size rather than scaled afterwards, so its pixels stay even and its outline stays
// 1 pixel wide. Elites use the larger steps of their own type.
inline constexpr int kScaleSteps = 6;
inline constexpr float kStepScale[kScaleSteps] = {0.78f, 0.88f, 1.0f, 1.14f, 1.3f, 1.48f};
inline constexpr int kStepNormalLast = 4;
inline constexpr int kStepEliteFirst = 3;
// The boss is so big that one size for the far half of the screen goes unnoticed.
inline constexpr int kStepBossFirst = 2;
inline constexpr int kStepBossLast = 3;

inline constexpr int kCorpseFrames = 3;

// Soldier.
inline constexpr int kSoldierWalkFrames = 8;
inline constexpr int kSoldierIdlePose = kSoldierWalkFrames;  // Two breathing poses follow the walk.
inline constexpr int kSoldierDeadPose = kSoldierWalkFrames + 2;
inline constexpr int kSoldierPoses = kSoldierWalkFrames + 3;
inline constexpr int kGunAngles = 9;  // From straight up to straight down. Left is mirrored.
inline constexpr int kReloadFrames = 2;
inline constexpr int kGunCount = static_cast<int>(WeaponId::Count);

// Small items.
enum class Item : uint8_t {
    GemSmall, GemLarge, GemHuge,
    PickupHeal, PickupMagnet, PickupBomb, PickupAmmo,
    MineOff, MineOn,
    Grenade,
    EnemyBulletA, EnemyBulletB,
    MuzzleSmall, MuzzleBig,
    ShadowTiny, ShadowSmall, ShadowMedium, ShadowLarge, ShadowHuge, ShadowBoss,
    FireA, FireB, FireC, FireD,
    LampBulb,
    LightPool,
    Puff0, Puff1, Puff2, Puff3, Puff4, Puff5, Puff6, Puff7,
    Count,
};
inline constexpr int kRocketAngles = 16;
inline constexpr int kBladeFrames = 8;

const SpriteRef& figureSprite(Figure figure, int step, int facing, int pose);
const SpriteRef& corpseSprite(Figure figure, int step, int frame);
const SpriteRef& soldierBody(int facing, int pose);
// armsDown is the mirror-free angle index, 0 pointing up the screen and 8 down.
const SpriteRef& soldierGun(WeaponId gun, int angleIndex);
const SpriteRef& soldierReload(WeaponId gun, int frame);
// How far ahead of the shoulder the gun's muzzle is, in art pixels.
float gunLength(WeaponId gun);
const SpriteRef& itemSprite(Item item);
const SpriteRef& rocketSprite(int angleIndex);
const SpriteRef& bladeSprite(int frame);

// The boss has twelve looks, three phases for each difficulty. Painting all of them at
// startup would cost too much time and atlas space, and a run only ever meets one
// difficulty. So the atlas holds one difficulty's boss at a time. This repaints it, and
// its palette rows, when a run of another difficulty begins. Calling it again with the
// same difficulty costs nothing, so the renderer can call it every frame.
void useBossLevel(Difficulty level);
// How much bigger than Easy each difficulty draws the boss. Its body for the game stays
// the same size.
float bossArtScale(Difficulty level);

// Builds the atlas and palette textures. Needs a live graphics context.
// Returns how many atlas rows were used, out of kAtlasSize. 0 means it overflowed.
int buildAtlas();
unsigned int atlasTextureId();
unsigned int paletteTextureId();
inline constexpr int kAtlasSize = 2048;

// Painters, one per family of sprites. Each draws one frame into the canvas.
// bossLevel only matters for the boss figures.
void paintFigure(Canvas& canvas, Figure figure, Difficulty bossLevel, int facing, int pose);
void paintCorpse(Canvas& canvas, Figure figure, Difficulty bossLevel, int frame);
void paintSoldierBody(Canvas& canvas, int facing, int pose);
void paintSoldierGun(Canvas& canvas, WeaponId gun, float angleRadians);
void paintSoldierReload(Canvas& canvas, WeaponId gun, int frame);
void paintItem(Canvas& canvas, Item item);
void paintRocket(Canvas& canvas, float angleRadians);
void paintBlade(Canvas& canvas, float angleRadians);

}  // namespace ls::worldview
