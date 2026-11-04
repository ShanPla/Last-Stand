#include "components/WorldView/Sprites.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>

// Rewards and small effects. Rewards are round, threats are sharp, so they differ by
// shape as well as color. Small icons are written as string rows, as a pixel artist
// would place them. A letter maps to a palette position through a small table.
namespace ls::worldview {

namespace {

constexpr Ramp kGemSmallRamp{kPGemSmall, 4};
constexpr Ramp kGemLargeRamp{kPGemLarge, 4};
constexpr Ramp kGemHugeRamp{kPGemHuge, 4};
constexpr Ramp kCrate{kPCrate, 3};
constexpr Ramp kMineRamp{kPMine, 3};
constexpr Ramp kGrenadeRamp{kPGrenade, 3};
constexpr Ramp kRocketRamp{kPRocket, 3};
constexpr Ramp kBladeRamp{kPBlade, 3};


constexpr Ramp kShadowRamp{kPOutline, 1};

struct IconTable {
    uint8_t map[128] = {};
    constexpr IconTable() {
        map[static_cast<int>('w')] = kPIcon;
        map[static_cast<int>('e')] = kPBadgeEdge;
        map[static_cast<int>('s')] = kPBadgeShade;
        map[static_cast<int>('f')] = kPBadgeFill;
        map[static_cast<int>('h')] = kPHeal;
        map[static_cast<int>('m')] = kPMagnet;
        map[static_cast<int>('b')] = kPBomb;
        map[static_cast<int>('a')] = kPAmmo;
        map[static_cast<int>('c')] = kPCrate;
        map[static_cast<int>('d')] = kPCrate + 1;
        map[static_cast<int>('l')] = kPCrate + 2;
        map[static_cast<int>('y')] = kPBrass;
        map[static_cast<int>('o')] = kPOutline;
    }
};
constexpr IconTable kIcons{};

constexpr const char* kHealIcon[] = {
    "...hhhh...",
    "...hwwh...",
    "...hwwh...",
    "hhhhwwhhhh",
    "hwwwwwwwwh",
    "hwwwwwwwwh",
    "hhhhwwhhhh",
    "...hwwh...",
    "...hwwh...",
    "...hhhh...",
};

constexpr const char* kMagnetIcon[] = {
    ".wwm..mww.",
    ".wwm..mww.",
    ".mmm..mmm.",
    ".mmm..mmm.",
    ".mmm..mmm.",
    ".mmm..mmm.",
    ".mmmmmmmm.",
    "..mmmmmm..",
    "...mmmm...",
};

constexpr const char* kBombIcon[] = {
    ".......wbw",
    "......b.w.",
    ".....b....",
    "...ffff...",
    "..fwfbbff.",
    ".fwfbbbbff",
    ".fbbbbbbbf",
    ".fbbbbbbbf",
    ".fbbbbbbff",
    "..fbbbbff.",
    "...ffff...",
};

constexpr const char* kCrateIcon[] = {
    "oooooooooooooooo",
    "oeeeeeeeeeeeeeeo",
    "oellllllllllllso",
    "oeldddddddddddso",
    "oeldydddyydddcso",
    "oelddddwyyydddso",
    "oelddddwyyydddso",
    "oelddddyyyydddso",
    "oeldddaaaaaddcso",
    "oeldddaaaaaddcso",
    "oeldddaaaaaddcso",
    "oeldydddddddyddo",
    "oeldddddddddddso",
    "oescccccccccccso",
    "osssssssssssssso",
    "oooooooooooooooo",
};

template <int N>
void stampCentered(Canvas& c, const char* const (&rows)[N], float height) {
    int width = 0;
    while (rows[0][width] != '\0') ++width;
    c.stamp(rows, N, -static_cast<float>(width) * 0.5f, height + static_cast<float>(N) * 0.5f, kIcons.map);
}

// The shared round badge. A bright rim marks it as a reward at a glance.
void paintBadge(Canvas& c, uint8_t fillTone) {
    constexpr float kRadius = 10.0f;
    c.ball(0.0f, 10.0f, kRadius, kRadius, Ramp{kPBadgeEdge, 1});
    c.ball(0.0f, 10.0f, kRadius - 1.0f, kRadius - 1.0f, Ramp{kPBadgeShade, 1});
    c.ball(0.0f, 10.0f, kRadius - 2.0f, kRadius - 2.0f, Ramp{fillTone, 1});
}

void paintGem(Canvas& c, Ramp ramp, float radius) {
    c.ball(0.0f, radius + 1.0f, radius, radius, ramp);
    c.dot(-radius * 0.35f, radius * 1.5f + 1.0f, static_cast<uint8_t>(ramp.first + 3));
    c.outline(kPOutline);
}

void paintShadow(Canvas& c, float rx, float ry) {
    c.ball(0.0f, 0.0f, rx, ry, kShadowRamp);
}

void paintEnemyBullet(Canvas& c, float size) {
    // A spiked star, so it can't be mistaken for a round gem, even with no color.
    const auto star = [&](float s, uint8_t index) {
        const Ramp ramp{index, 1};
        c.limb(-s, 0.0f, s, 0.0f, s * 0.42f, s * 0.42f, ramp);
        c.limb(0.0f, -s, 0.0f, s, s * 0.42f, s * 0.42f, ramp);
        c.ball(0.0f, 0.0f, s * 0.72f, s * 0.72f, ramp);
    };
    star(size + 1.0f, kPEnemyRim);
    star(size, kPEnemyHot);
    c.ball(0.0f, 0.0f, size * 0.62f, size * 0.62f, Ramp{kPEnemyHotLight, 1});
    c.ball(0.0f, 0.0f, size * 0.36f + 0.4f, size * 0.36f + 0.4f, Ramp{kPEnemyCore, 1});
}

void paintMuzzle(Canvas& c, float size) {
    // Pointed in every direction, so one sprite suits any aim.
    const Ramp outer{kPMuzzle, 1};
    c.limb(-size, 0.0f, size, 0.0f, 0.5f, 0.5f, outer);
    c.limb(0.0f, -size, 0.0f, size, 0.5f, 0.5f, outer);
    c.limb(-size * 0.6f, -size * 0.6f, size * 0.6f, size * 0.6f, 0.5f, 0.5f, outer);
    c.limb(-size * 0.6f, size * 0.6f, size * 0.6f, -size * 0.6f, 0.5f, 0.5f, outer);
    c.ball(0.0f, 0.0f, size * 0.5f, size * 0.5f, Ramp{static_cast<uint8_t>(kPMuzzle + 1), 1});
    c.ball(0.0f, 0.0f, size * 0.26f + 0.3f, size * 0.26f + 0.3f, Ramp{static_cast<uint8_t>(kPMuzzle + 2), 1});
}

// A lumpy ball with a bright middle. One shape does for flames, blasts and smoke, as
// a palette row recolors it, so hundreds of them need only these few sprites.
void paintPuff(Canvas& c, float radius, int variant) {
    const Ramp outer{kPFlame, 1};
    const Ramp middle{static_cast<uint8_t>(kPFlame + 1), 1};
    const Ramp core{static_cast<uint8_t>(kPFlame + 2), 1};
    const float flip = variant == 0 ? 1.0f : -1.0f;
    c.ball(0.0f, 0.0f, radius * 0.86f, radius * 0.8f, outer);
    c.ball(flip * radius * 0.5f, radius * 0.42f, radius * 0.5f, radius * 0.5f, outer);
    c.ball(-flip * radius * 0.52f, -radius * 0.18f, radius * 0.46f, radius * 0.46f, outer);
    c.ball(flip * radius * 0.1f, -radius * 0.52f, radius * 0.42f, radius * 0.42f, outer);
    c.ball(flip * radius * 0.08f, radius * 0.05f, radius * 0.56f, radius * 0.52f, middle);
    c.ball(-flip * radius * 0.08f, -radius * 0.08f, radius * 0.28f, radius * 0.26f, core);
}

// A pool of light with a dithered edge: bands of brightness that break up into a
// checkered pattern between them, the way pixel art fakes a soft gradient.
void paintLightPool(Canvas& c) {
    constexpr int kRadiusX = 62;
    constexpr int kRadiusY = 36;
    constexpr float kBayer[4][4] = {{0.03f, 0.53f, 0.15f, 0.65f}, {0.78f, 0.28f, 0.90f, 0.40f}, {0.21f, 0.71f, 0.09f, 0.59f}, {0.96f, 0.46f, 0.84f, 0.34f}};
    for (int y = -kRadiusY; y <= kRadiusY; ++y) {
        for (int x = -kRadiusX; x <= kRadiusX; ++x) {
            const float nx = static_cast<float>(x) / static_cast<float>(kRadiusX);
            const float ny = static_cast<float>(y) / static_cast<float>(kRadiusY);
            const float strength = (1.0f - std::sqrt(nx * nx + ny * ny)) * 3.4f;
            const int level = static_cast<int>(strength + kBayer[(y + 400) & 3][(x + 400) & 3]);
            if (strength <= 0.0f || level <= 0) continue;
            c.dot(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, static_cast<uint8_t>(kPLight + std::min(level, 3) - 1));
        }
    }
}

void paintFire(Canvas& c, int frame) {
    // Four frames of a small flame, for burning barrels and campfires.
    constexpr float kLean[4] = {-1.0f, 1.0f, 0.0f, 1.5f};
    constexpr float kTall[4] = {9.0f, 11.0f, 8.0f, 10.0f};
    const float lean = kLean[frame];
    const float tall = kTall[frame];
    c.limb(0.0f, 2.0f, lean, tall, 3.2f, 0.5f, Ramp{kPFlame, 1});
    c.limb(-1.5f, 1.5f, lean - 2.0f, tall * 0.6f, 1.6f, 0.4f, Ramp{kPFlame, 1});
    c.limb(1.5f, 1.5f, lean + 2.0f, tall * 0.5f, 1.6f, 0.4f, Ramp{kPFlame, 1});
    c.limb(0.0f, 2.0f, lean * 0.7f, tall * 0.7f, 2.0f, 0.4f, Ramp{static_cast<uint8_t>(kPFlame + 1), 1});
    c.ball(0.0f, 2.0f, 1.2f, 1.6f, Ramp{static_cast<uint8_t>(kPFlame + 2), 1});
}

}  // namespace

void paintItem(Canvas& c, Item item) {
    switch (item) {
        case Item::GemSmall: paintGem(c, kGemSmallRamp, 2.1f); break;
        case Item::GemLarge: paintGem(c, kGemLargeRamp, 3.8f); break;
        case Item::GemHuge: paintGem(c, kGemHugeRamp, 5.2f); break;
        case Item::PickupHeal:
            paintBadge(c, kPBadgeFill);
            stampCentered(c, kHealIcon, 10.0f);
            c.outline(kPOutline);
            break;
        case Item::PickupMagnet:
            paintBadge(c, kPBadgeFill);
            stampCentered(c, kMagnetIcon, 10.0f);
            c.outline(kPOutline);
            break;
        case Item::PickupBomb:
            paintBadge(c, kPBadgeFill);
            stampCentered(c, kBombIcon, 10.0f);
            c.outline(kPOutline);
            break;
        case Item::PickupAmmo:
            stampCentered(c, kCrateIcon, 10.0f);
            break;
        case Item::MineOff:
        case Item::MineOn:
            c.ball(0.0f, 1.5f, 7.5f, 3.6f, kMineRamp);
            c.ball(0.0f, 2.8f, 4.5f, 2.0f, kMineRamp, 1);
            c.ball(0.0f, 3.0f, 1.8f, 1.1f, Ramp{static_cast<uint8_t>(item == Item::MineOn ? kPMineOn : kPMineOff), 1});
            c.outline(kPOutline);
            break;
        case Item::Grenade:
            c.ball(0.0f, 0.0f, 3.4f, 3.8f, kGrenadeRamp);
            c.patch(-1.0f, 5.0f, 2.0f, 1.0f, kPMine);
            c.patch(-3.0f, 1.0f, 6.0f, 1.0f, kPGrenade);
            c.outline(kPOutline);
            break;
        case Item::EnemyBulletA: paintEnemyBullet(c, 5.0f); break;
        case Item::EnemyBulletB: paintEnemyBullet(c, 5.8f); break;
        case Item::MuzzleSmall: paintMuzzle(c, 5.0f); break;
        case Item::MuzzleBig: paintMuzzle(c, 8.0f); break;
        case Item::ShadowTiny: paintShadow(c, 5.0f, 2.0f); break;
        case Item::ShadowSmall: paintShadow(c, 8.0f, 3.0f); break;
        case Item::ShadowMedium: paintShadow(c, 11.0f, 4.0f); break;
        case Item::ShadowLarge: paintShadow(c, 17.0f, 6.0f); break;
        case Item::ShadowHuge: paintShadow(c, 22.0f, 8.0f); break;
        case Item::ShadowBoss: paintShadow(c, 46.0f, 16.0f); break;
        case Item::FireA: paintFire(c, 0); break;
        case Item::FireB: paintFire(c, 1); break;
        case Item::FireC: paintFire(c, 2); break;
        case Item::FireD: paintFire(c, 3); break;
        case Item::LightPool: paintLightPool(c); break;
        case Item::LampBulb:
            c.ball(0.0f, 0.0f, 3.0f, 1.6f, Ramp{static_cast<uint8_t>(kPMuzzle + 1), 1});
            c.ball(0.0f, 0.0f, 1.5f, 0.8f, Ramp{static_cast<uint8_t>(kPMuzzle + 2), 1});
            break;
        case Item::Puff0: paintPuff(c, 3.0f, 0); break;
        case Item::Puff1: paintPuff(c, 3.0f, 1); break;
        case Item::Puff2: paintPuff(c, 5.0f, 0); break;
        case Item::Puff3: paintPuff(c, 5.0f, 1); break;
        case Item::Puff4: paintPuff(c, 8.0f, 0); break;
        case Item::Puff5: paintPuff(c, 8.0f, 1); break;
        case Item::Puff6: paintPuff(c, 12.0f, 0); break;
        case Item::Puff7: paintPuff(c, 12.0f, 1); break;
        case Item::Count: break;
    }
}

void paintRocket(Canvas& c, float angle) {
    c.setFrame(0.0f, 0.0f, std::cos(angle), std::sin(angle));
    c.box(-6.0f, -3.0f, -3.5f, 3.0f, kRocketRamp, -1);  // Fins.
    c.box(-5.0f, -1.6f, 4.0f, 1.6f, kRocketRamp);
    c.box(4.0f, -1.2f, 6.5f, 1.2f, Ramp{kPRocketNose, 1});
    c.clearFrame();
    c.outline(kPOutline);
}

void paintBlade(Canvas& c, float angle) {
    // A two-pointed combat blade, seen spinning. Its long side turns with the frame.
    c.setFrame(0.0f, 0.0f, std::cos(angle), std::sin(angle));
    c.limb(-1.0f, 0.0f, 10.0f, 0.0f, 2.2f, 0.4f, kBladeRamp);
    c.limb(1.0f, 0.0f, -10.0f, 0.0f, 2.2f, 0.4f, kBladeRamp);
    c.clearFrame();
    c.ball(0.0f, 0.0f, 2.0f, 2.0f, kBladeRamp, -1);
    c.ball(0.0f, 0.0f, 0.8f, 0.8f, kBladeRamp, 2);
    c.outline(kPOutline);
}

}  // namespace ls::worldview
