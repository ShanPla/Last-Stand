#include "components/WorldView/Sprites.h"
#include "core/Platform.h"
#include "data/Theme.h"

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ls::worldview {

namespace {

SpriteRef g_figures[kFigureCount][kScaleSteps][kFacingCount][kPoseCount];
SpriteRef g_corpses[kFigureCount][kScaleSteps][kCorpseFrames];
SpriteRef g_soldier[kFacingCount][kSoldierPoses];
SpriteRef g_guns[kGunCount][kGunAngles];
SpriteRef g_reloads[kGunCount][kReloadFrames];
SpriteRef g_items[static_cast<int>(Item::Count)];
SpriteRef g_rockets[kRocketAngles];
SpriteRef g_blades[kBladeFrames];
unsigned int g_atlasTexture = 0;
unsigned int g_paletteTexture = 0;

// The boss's part of the atlas: every row from g_bossTop down. Its sprites are painted
// again when the difficulty changes, so they sit below everything that never changes.
int g_bossTop = 0;
int g_bossRows = 0;
Difficulty g_bossLevel = Difficulty::Normal;

// Rows of sprites, left to right. Frames of one figure have similar heights, so this
// simple packer leaves little waste, and needs no sorting. pixels holds the atlas from
// firstRow down, so the boss's part can be repainted in a smaller buffer of its own.
class Packer {
public:
    Packer(uint8_t* pixels, int firstRow) : pixels_(pixels), firstRow_(firstRow), penY_(firstRow) {}

    SpriteRef add(const Canvas& canvas) {
        SpriteRef ref;
        int left, top, right, bottom;
        if (!canvas.bounds(&left, &top, &right, &bottom)) return ref;
        const int width = right - left + 1;
        const int height = bottom - top + 1;
        // A boss drawing that reaches the canvas edge has been cut off. The boss grows
        // with the difficulty, so this catches a level drawn too big.
        if (checkEdges_ && (left == 0 || top == 0 || right == Canvas::kSize - 1 || bottom == Canvas::kSize - 1)) clipped_ = true;
        if (penX_ + width + kGap > kAtlasSize) {
            penX_ = 1;
            penY_ += rowHeight_ + kGap;
            rowHeight_ = 0;
        }
        if (penY_ + height + kGap > kAtlasSize) {
            overflowed_ = true;
            return ref;
        }
        const uint8_t* source = canvas.pixels();
        for (int y = 0; y < height; ++y) {
            std::memcpy(pixels_ + (penY_ - firstRow_ + y) * kAtlasSize + penX_, source + (top + y) * Canvas::kSize + left, static_cast<size_t>(width));
        }
        ref.x = static_cast<uint16_t>(penX_);
        ref.y = static_cast<uint16_t>(penY_);
        ref.w = static_cast<uint8_t>(width);
        ref.h = static_cast<uint8_t>(height);
        ref.footX = static_cast<uint8_t>(canvas.originX() - left);
        ref.footY = static_cast<uint8_t>(std::max(0, canvas.originY() - top));
        penX_ += width + kGap;
        rowHeight_ = std::max(rowHeight_, height);
        return ref;
    }

    int usedRows() const { return penY_ + rowHeight_; }
    bool overflowed() const { return overflowed_; }
    bool clipped() const { return clipped_; }
    void checkEdges() { checkEdges_ = true; }
    // Where a part of the atlas that starts on a row of its own begins.
    int startRow() {
        if (penX_ > 1) {
            penY_ += rowHeight_ + kGap;
            penX_ = 1;
            rowHeight_ = 0;
        }
        return penY_;
    }

private:
    static constexpr int kGap = 1;  // Stops a neighbor bleeding in at a rounding edge.
    uint8_t* pixels_;
    int firstRow_;
    int penX_ = 1;
    int penY_;
    int rowHeight_ = 0;
    bool overflowed_ = false;
    bool clipped_ = false;
    bool checkEdges_ = false;
};

// Elites only exist for some types, and don't need every step.
bool hasElites(Figure figure) {
    return figure == Figure::Walker || figure == Figure::Brute || figure == Figure::Spitter || figure == Figure::Bloater ||
           figure == Figure::Charger;
}

int posesOf(Figure figure) {
    switch (figure) {
        case Figure::Spitter: return kPoseSpecial + 2;
        case Figure::Charger: return kPoseCount;
        default: return kWalkFrames;
    }
}

// Every zombie but the boss.
void buildFigures(Packer& packer, Canvas& canvas) {
    for (int f = 0; f < static_cast<int>(Figure::Boss1); ++f) {
        const Figure figure = static_cast<Figure>(f);
        for (int step = 0; step < kScaleSteps; ++step) {
            const bool normal = step <= kStepNormalLast;
            if (!normal && !(hasElites(figure) && step >= kStepEliteFirst)) continue;
            for (int facing = 0; facing < kFacingCount; ++facing) {
                for (int pose = 0; pose < posesOf(figure); ++pose) {
                    canvas.begin(kStepScale[step]);
                    paintFigure(canvas, figure, Difficulty::Normal, facing, pose);
                    g_figures[f][step][facing][pose] = packer.add(canvas);
                }
            }
            if (!normal) continue;
            for (int frame = 0; frame < kCorpseFrames; ++frame) {
                canvas.begin(kStepScale[step]);
                paintCorpse(canvas, figure, Difficulty::Normal, frame);
                g_corpses[f][step][frame] = packer.add(canvas);
            }
        }
    }
}

// The boss's three phases for one difficulty. It always dies in its last phase, so only
// that phase needs a corpse.
void buildBoss(Packer& packer, Canvas& canvas, Difficulty level) {
    const float size = bossArtScale(level);
    // Hard and Hell are drawn so much bigger that two sizes of them don't fit in the
    // atlas. They get one size halfway between the two, used near and far: at most 7% off
    // the true perspective size, which is hard to see on a figure this big.
    const bool oneSize = level >= Difficulty::Hard;
    const float middle = (kStepScale[kStepBossFirst] + kStepScale[kStepBossLast]) * 0.5f;
    // Pose by pose, so frames of a similar height share a row of the atlas. The packer
    // wastes the space above short frames in a row of tall ones, and the boss's frames
    // differ in height a lot, for example with its arms raised to charge.
    for (int step = oneSize ? kStepBossLast : kStepBossFirst; step <= kStepBossLast; ++step) {
        const float scale = (oneSize ? middle : kStepScale[step]) * size;
        for (int pose = 0; pose < kPoseCount; ++pose) {
            const bool wanted = pose < kBossWalkFrames || pose == kPoseBraceA || pose == kPoseBraceB || pose == kPoseDashA;
            if (!wanted) continue;
            for (int facing = 0; facing < kFacingCount; ++facing) {
                for (int phase = 0; phase < 3; ++phase) {
                    const Figure figure = static_cast<Figure>(static_cast<int>(Figure::Boss1) + phase);
                    canvas.begin(scale);
                    paintFigure(canvas, figure, level, facing, pose);
                    g_figures[static_cast<int>(figure)][step][facing][pose] = packer.add(canvas);
                }
            }
        }
        const int last = static_cast<int>(Figure::Boss3);
        for (int frame = 0; frame < kCorpseFrames; ++frame) {
            canvas.begin(scale);
            paintCorpse(canvas, Figure::Boss3, level, frame);
            g_corpses[last][step][frame] = packer.add(canvas);
        }
    }
    if (!oneSize) return;
    for (int f = static_cast<int>(Figure::Boss1); f <= static_cast<int>(Figure::Boss3); ++f) {
        for (int step = kStepBossFirst; step < kStepBossLast; ++step) {
            for (int facing = 0; facing < kFacingCount; ++facing) {
                for (int pose = 0; pose < kPoseCount; ++pose) g_figures[f][step][facing][pose] = g_figures[f][kStepBossLast][facing][pose];
            }
            for (int frame = 0; frame < kCorpseFrames; ++frame) g_corpses[f][step][frame] = g_corpses[f][kStepBossLast][frame];
        }
    }
}

void buildSoldier(Packer& packer, Canvas& canvas) {
    for (int facing = 0; facing < kFacingCount; ++facing) {
        for (int pose = 0; pose < kSoldierPoses; ++pose) {
            canvas.begin(1.0f);
            paintSoldierBody(canvas, facing, pose);
            g_soldier[facing][pose] = packer.add(canvas);
        }
    }
    for (int gun = 0; gun < kGunCount; ++gun) {
        for (int angle = 0; angle < kGunAngles; ++angle) {
            const float radians = kPi * 0.5f - kPi * static_cast<float>(angle) / static_cast<float>(kGunAngles - 1);
            canvas.begin(1.0f);
            paintSoldierGun(canvas, static_cast<WeaponId>(gun), radians);
            g_guns[gun][angle] = packer.add(canvas);
        }
        for (int frame = 0; frame < kReloadFrames; ++frame) {
            canvas.begin(1.0f);
            paintSoldierReload(canvas, static_cast<WeaponId>(gun), frame);
            g_reloads[gun][frame] = packer.add(canvas);
        }
    }
}

void buildItems(Packer& packer, Canvas& canvas) {
    for (int i = 0; i < static_cast<int>(Item::Count); ++i) {
        canvas.begin(1.0f);
        paintItem(canvas, static_cast<Item>(i));
        g_items[i] = packer.add(canvas);
    }
    for (int i = 0; i < kRocketAngles; ++i) {
        canvas.begin(1.0f);
        paintRocket(canvas, kTau * static_cast<float>(i) / static_cast<float>(kRocketAngles));
        g_rockets[i] = packer.add(canvas);
    }
    for (int i = 0; i < kBladeFrames; ++i) {
        canvas.begin(1.0f);
        paintBlade(canvas, kPi * static_cast<float>(i) / static_cast<float>(kBladeFrames));
        g_blades[i] = packer.add(canvas);
    }
}

// --- Palette ------------------------------------------------------------------

Color g_palette[kPaletteHeight][kPaletteWidth];

void setRamp4(Color* row, int column, const theme::Ramp4& ramp) {
    for (int i = 0; i < 4; ++i) row[column + i] = ramp.tone[i];
}
void setRamp3(Color* row, int column, const theme::Ramp3& ramp) {
    for (int i = 0; i < 3; ++i) row[column + i] = ramp.tone[i];
}
void setRamp2(Color* row, int column, const theme::Ramp2& ramp) {
    for (int i = 0; i < 2; ++i) row[column + i] = ramp.tone[i];
}

// Every row starts from this: fixed materials like metal, guns and rewards.
void fillBaseRow(Color* row) {
    using namespace theme;
    row[kPClear] = kNoColor;
    row[kPOutline] = kOutline;
    setRamp4(row, kPSkin, kZombieSkins[0]);
    setRamp2(row, kPRot, kZombieRot[0]);
    setRamp3(row, kPHair, kZombieHair[0]);
    setRamp4(row, kPShirt, kZombieShirts[0]);
    setRamp4(row, kPPants, kZombiePants[0]);
    setRamp2(row, kPShoes, kZombieShoes[0]);
    setRamp3(row, kPBlood, kBloodRamp);
    row[kPSocket] = kSocket;
    row[kPGlint] = kEyeGlint;
    row[kPMouth] = kMouthDark;
    row[kPTeeth] = kTeeth;
    setRamp4(row, kPMetal, kMetalRamp);
    setRamp4(row, kPBone, kBoneRamp);
    setRamp4(row, kPBile, kBileRamp);
    setRamp4(row, kPHelmet, kSoldierHelmet);
    setRamp2(row, kPCamo, kSoldierCamo);
    setRamp4(row, kPVest, kSoldierVest);
    setRamp4(row, kPSoldierPants, kSoldierPants);
    setRamp3(row, kPBoots, kSoldierBoots);
    setRamp2(row, kPGloves, kSoldierGloves);
    setRamp4(row, kPFace, kSoldierFace);
    setRamp2(row, kPStrap, kSoldierStrap);
    setRamp3(row, kPGun, kGunRamp);
    setRamp2(row, kPWood, kGunWood);
    row[kPGunAccent] = kGunAccent;
    row[kPGunTank] = kGunTank;
    setRamp4(row, kPGemSmall, kGemSmall);
    setRamp4(row, kPGemLarge, kGemLarge);
    setRamp4(row, kPGemHuge, kGemHuge);
    row[kPBadgeEdge] = kBadgeEdge;
    row[kPBadgeShade] = kBadgeShade;
    row[kPBadgeFill] = kBadgeFill;
    row[kPHeal] = kPickupHeal;
    row[kPMagnet] = kPickupMagnet;
    row[kPBomb] = kPickupBomb;
    row[kPAmmo] = kPickupAmmo;
    row[kPIcon] = kPickupIcon;
    setRamp3(row, kPCrate, kCrateWood);
    row[kPBrass] = kBulletBrass;
    setRamp3(row, kPMine, kMineBody);
    row[kPMineOn] = kMineLightOn;
    row[kPMineOff] = kMineLightOff;
    setRamp3(row, kPGrenade, kGrenadeBody);
    setRamp3(row, kPRocket, kRocketBody);
    row[kPRocketNose] = kRocketNose;
    setRamp3(row, kPBlade, kBladeSteel);
    setRamp3(row, kPMuzzle, kMuzzleRamp);
    row[kPEnemyRim] = kEnemyBulletRim;
    row[kPEnemyHot] = kEnemyBullet;
    row[kPEnemyHotLight] = kEnemyBulletHot;
    row[kPEnemyCore] = kEnemyBulletCore;
    for (int i = 0; i < 3; ++i) row[kPFlame + i] = kFireFlame[i];
    setRamp4(row, kPGrowth, kBossGrowthEasy);
}

// Clothes in a ruined city have faded. Pulling them toward gray keeps a big crowd from
// looking like a box of sweets, while each outfit still reads as its own color.
void setFaded(Color* row, int column, const theme::Ramp4& ramp) {
    constexpr int kKeepPercent = 76;
    for (int i = 0; i < 4; ++i) {
        const Color c = ramp.tone[i];
        const int gray = (c.r * 30 + c.g * 59 + c.b * 11) / 100;
        row[column + i] = Color{static_cast<unsigned char>(gray + (c.r - gray) * kKeepPercent / 100), static_cast<unsigned char>(gray + (c.g - gray) * kKeepPercent / 100),
                                static_cast<unsigned char>(gray + (c.b - gray) * kKeepPercent / 100), 255};
    }
}

void setHairFromSkin(Color* row, const theme::Ramp4& skin) {
    // Bald: the hair ramp is just more skin.
    for (int i = 0; i < 3; ++i) row[kPHair + i] = skin.tone[i];
}

void buildPalette() {
    using namespace theme;
    constexpr int kSkinCount = static_cast<int>(sizeof(kZombieSkins) / sizeof(kZombieSkins[0]));
    constexpr int kShirtCount = static_cast<int>(sizeof(kZombieShirts) / sizeof(kZombieShirts[0]));
    constexpr int kPantsCount = static_cast<int>(sizeof(kZombiePants) / sizeof(kZombiePants[0]));
    constexpr int kHairCount = static_cast<int>(sizeof(kZombieHair) / sizeof(kZombieHair[0]));
    constexpr int kShoesCount = static_cast<int>(sizeof(kZombieShoes) / sizeof(kZombieShoes[0]));

    for (int r = 0; r < kPaletteHeight; ++r) fillBaseRow(g_palette[r]);

    // Co-op soldiers: the same soldier, with his helmet and vest in his player's color.
    for (int r = 0; r < kTeamRows; ++r) {
        setRamp4(g_palette[kRowTeam + r], kPHelmet, kCoopSoldierHelmets[r]);
        setRamp4(g_palette[kRowTeam + r], kPVest, kCoopSoldierVests[r]);
    }

    // Outfits. Stepping each list by a different amount covers many combinations.
    for (int r = 0; r < kOutfitCount; ++r) {
        Color* row = g_palette[kRowOutfits + r];
        const int skin = r % kSkinCount;
        setRamp4(row, kPSkin, kZombieSkins[skin]);
        setRamp2(row, kPRot, kZombieRot[skin]);
        setFaded(row, kPShirt, kZombieShirts[(r * 3 + r / 8) % kShirtCount]);
        setFaded(row, kPPants, kZombiePants[(r * 2 + r / 5) % kPantsCount]);
        setRamp2(row, kPShoes, kZombieShoes[r % kShoesCount]);
        const int hair = (r * 3 + 1) % (kHairCount + 1);
        if (hair == kHairCount) setHairFromSkin(row, kZombieSkins[skin]);
        else setRamp3(row, kPHair, kZombieHair[hair]);
    }

    // Elites: a strong gold outfit and outline on the same figure.
    for (int r = 0; r < 8; ++r) {
        Color* row = g_palette[kRowElite + r];
        row[kPOutline] = kEliteOutline;
        setRamp4(row, kPSkin, kZombieSkins[r % kSkinCount]);
        setRamp4(row, kPShirt, kEliteShirt);
        setRamp4(row, kPPants, kElitePants);
    }

    // The shadow row paints every used position in one color. The hit flash isn't a
    // row: the sprite shader mixes it in, so a flashing zombie keeps its own outfit.
    for (int c = 1; c < kPaletteWidth; ++c) {
        g_palette[kRowShadow][c] = kSpriteShadow;
        // The telegraph flash keeps some of the shading, so the pose still reads.
        const Color base = g_palette[kRowOutfits][c];
        g_palette[kRowHot][c] = Color{static_cast<unsigned char>((base.r + kFlashHot.r * 2) / 3), static_cast<unsigned char>((base.g + kFlashHot.g * 2) / 3),
                                      static_cast<unsigned char>((base.b + kFlashHot.b * 2) / 3), 255};
    }
    g_palette[kRowHot][kPOutline] = kOutline;

    const auto setFlame = [](int rowIndex, Color outer, Color middle, Color core) {
        g_palette[rowIndex][kPFlame] = outer;
        g_palette[rowIndex][kPFlame + 1] = middle;
        g_palette[rowIndex][kPFlame + 2] = core;
    };
    setFlame(kRowEvolved, kFlameEvolvedOuter, kFlameEvolvedMiddle, kFlameEvolvedCore);
    setFlame(kRowFireHot, kBlastYellow, kBlastWhite, kBlastWhite);
    setFlame(kRowFireOld, kBlastRed, kBlastOrange, kBlastYellow);
    setFlame(kRowSmoke, kSmoke, kSmoke, kSmokeLight);
    setFlame(kRowSmokeLight, kSmokeLight, kSmokeLight, kSmokeLight);

    // Light pools. They are multiplied onto the ground, so dark means no change.
    const auto setLight = [](int rowIndex, Color full) {
        for (int level = 1; level <= 3; ++level) {
            g_palette[rowIndex][kPLight + level - 1] = Color{static_cast<unsigned char>(full.r * level / 3), static_cast<unsigned char>(full.g * level / 3),
                                                             static_cast<unsigned char>(full.b * level / 3), 255};
        }
    };
    setLight(kRowLampLight, kLampLight);
    setLight(kRowFireLight, kFireLight);
    setLight(kRowPlayerLight, kPlayerLight);

    Color* evolved = g_palette[kRowEvolved];
    evolved[kPGunAccent] = kGunAccentEvolved;
    evolved[kPMineOn] = kMineLightOnEvolved;
    setRamp3(evolved, kPBlade, kBladeSteelEvolved);
    setRamp3(evolved, kPMuzzle, kMuzzleRampEvolved);

}

// The boss's phase rows and its charge-up row, for one difficulty.
void setBossRows(Difficulty level) {
    using namespace theme;
    const BossLook& look = kBossLooks[static_cast<int>(level)];
    for (int phase = 0; phase < 3; ++phase) {
        Color* row = g_palette[kRowBoss + phase];
        const BossPalette& boss = look.phase[phase];
        setRamp4(row, kPSkin, boss.skin);
        setRamp4(row, kPShirt, boss.cloth);
        setRamp4(row, kPPants, boss.pants);
        setRamp4(row, kPBile, boss.glow);
        setRamp4(row, kPGrowth, boss.growth);
        setRamp3(row, kPBlood, boss.blood);
        for (int i = 0; i < 3; ++i) row[kPHair + i] = boss.skin.tone[i];
        setRamp2(row, kPShoes, kZombieShoes[0]);
        row[kPOutline] = boss.outline;
    }
    // The charge-up paints the whole body in the warning glow, so it reads the same at
    // every difficulty however dark the boss's own colors are.
    Color* charge = g_palette[kRowBossCharge];
    setRamp4(charge, kPSkin, look.phase[2].glow);
    setRamp4(charge, kPShirt, look.phase[1].glow);
    setRamp4(charge, kPPants, look.phase[0].glow);
    setRamp4(charge, kPBile, look.phase[2].glow);
    setRamp4(charge, kPGrowth, look.phase[1].glow);
    for (int i = 0; i < 3; ++i) charge[kPHair + i] = look.phase[1].glow.tone[i];
    charge[kPOutline] = kBossChargeOutline;
}

}  // namespace

int buildAtlas() {
    uint8_t* pixels = new uint8_t[kAtlasSize * kAtlasSize]();
    Canvas* canvas = new Canvas();
    Packer packer(pixels, 0);
    buildSoldier(packer, *canvas);
    buildItems(packer, *canvas);
    buildFigures(packer, *canvas);
    // Normal first, because the title screen's demo plays Normal.
    g_bossTop = packer.startRow();
    packer.checkEdges();
    g_bossLevel = Difficulty::Normal;
    buildBoss(packer, *canvas, g_bossLevel);
    g_bossRows = packer.usedRows() - g_bossTop;
    if (packer.overflowed()) TraceLog(LOG_WARNING, "Sprite atlas is full: some sprites are missing");
    if (packer.clipped()) TraceLog(LOG_WARNING, "A boss sprite was too big for the canvas and was cut off");

    g_atlasTexture = rlLoadTexture(pixels, kAtlasSize, kAtlasSize, RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
    // Palette positions can't be blended, so the atlas must never be filtered.
    rlTextureParameters(g_atlasTexture, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(g_atlasTexture, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    delete canvas;
    delete[] pixels;
    const int usedRows = packer.overflowed() ? 0 : packer.usedRows();

    buildPalette();
    setBossRows(g_bossLevel);
    g_paletteTexture = rlLoadTexture(g_palette, kPaletteWidth, kPaletteHeight, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    rlTextureParameters(g_paletteTexture, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(g_paletteTexture, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    return usedRows;
}

void useBossLevel(Difficulty level) {
    if (level == g_bossLevel || g_atlasTexture == 0) return;
    const double start = GetTime();
    // A fresh buffer for the boss's rows only. This happens once when a run starts on
    // another difficulty, never during play, so the allocation can't cause a stutter.
    const int rows = kAtlasSize - g_bossTop;
    uint8_t* pixels = new uint8_t[static_cast<size_t>(rows) * kAtlasSize]();
    Canvas* canvas = new Canvas();
    Packer packer(pixels, g_bossTop);
    packer.checkEdges();
    buildBoss(packer, *canvas, level);
    const int used = packer.usedRows() - g_bossTop;
    if (packer.overflowed()) TraceLog(LOG_WARNING, "Sprite atlas is full: some boss sprites are missing");
    if (packer.clipped()) TraceLog(LOG_WARNING, "A boss sprite was too big for the canvas and was cut off");
    // As many rows as the taller of the old and new looks, which also wipes the old one.
    rlUpdateTexture(g_atlasTexture, 0, g_bossTop, kAtlasSize, std::min(rows, std::max(used, g_bossRows)), RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, pixels);
    delete canvas;
    delete[] pixels;
    g_bossRows = used;
    g_bossLevel = level;
    setBossRows(level);
    rlUpdateTexture(g_paletteTexture, 0, 0, kPaletteWidth, kPaletteHeight, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, g_palette);
    if (platform::debugEnabled()) {
        std::fprintf(stderr, "boss looks for difficulty %d painted in %.1f ms using %d rows from row %d\n", static_cast<int>(level), (GetTime() - start) * 1000.0, used, g_bossTop);
    }
}

unsigned int atlasTextureId() { return g_atlasTexture; }
unsigned int paletteTextureId() { return g_paletteTexture; }

const SpriteRef& figureSprite(Figure figure, int step, int facing, int pose) {
    return g_figures[static_cast<int>(figure)][step][facing][pose];
}
const SpriteRef& corpseSprite(Figure figure, int step, int frame) { return g_corpses[static_cast<int>(figure)][step][frame]; }
const SpriteRef& soldierBody(int facing, int pose) { return g_soldier[facing][pose]; }
const SpriteRef& soldierGun(WeaponId gun, int angleIndex) { return g_guns[static_cast<int>(gun)][angleIndex]; }
const SpriteRef& soldierReload(WeaponId gun, int frame) { return g_reloads[static_cast<int>(gun)][frame]; }
const SpriteRef& itemSprite(Item item) { return g_items[static_cast<int>(item)]; }
const SpriteRef& rocketSprite(int angleIndex) { return g_rockets[angleIndex]; }
const SpriteRef& bladeSprite(int frame) { return g_blades[frame]; }

}  // namespace ls::worldview
