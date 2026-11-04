#include "components/Ui/Icons/Icons.h"

#include "data/Theme.h"

#include <algorithm>
#include <cmath>

namespace ls::ui {

namespace {

// Every icon is a 12 by 12 grid of small squares, written out as rows of text:
//   #  the main color
//   +  a lighter tone, for highlights
//   o  a darker tone, for shading
//   .  nothing
// Drawing them at a whole number of screen pixels per square keeps them sharp.
constexpr int kGrid = 12;
using Rows = const char* const[kGrid];

// clang-format off
constexpr Rows kRifle = {
    "............",
    "............",
    "...o......o.",
    "oo#########+",
    "ooo#####o...",
    "oo..o+#o....",
    "o...o##.....",
    "....o#o.....",
    "....o#......",
    "............",
    "............",
    "............"};
constexpr Rows kShotgun = {
    "............",
    "............",
    "............",
    ".......#####",
    "ooo#########",
    "ooo+########",
    "oo.o####o...",
    "o..o+##o....",
    "...oo#o.....",
    "............",
    "............",
    "............"};
constexpr Rows kSmg = {
    "............",
    "............",
    "............",
    "..#########.",
    ".oo+######++",
    ".oo#########",
    ".o.o#o...o..",
    "...o#o......",
    "...o#o......",
    "...o#o......",
    "............",
    "............"};
constexpr Rows kSniper = {
    ".....##.....",
    ".....##.....",
    "...######...",
    "..##....##..",
    "..#......#..",
    "###..##..###",
    "###..##..###",
    "..#......#..",
    "..##....##..",
    "...######...",
    ".....##.....",
    ".....##....."};
constexpr Rows kFlamethrower = {
    "......#.....",
    ".....##.....",
    ".....###....",
    "....####.o..",
    "...#####.#..",
    "...###+###..",
    "..####++###.",
    "..###++++##.",
    "..###+++###.",
    "...###+###..",
    "....#####...",
    ".....###...."};
constexpr Rows kGrenade = {
    ".....oo.....",
    "....o##o.oo.",
    ".....##.o...",
    "...######...",
    "..###++###..",
    ".###+####o#.",
    ".###+####o#.",
    ".####o###o#.",
    "..####oo##..",
    "..#######o..",
    "...o####o...",
    ".....oo....."};
constexpr Rows kRocket = {
    ".........##.",
    "........####",
    ".......#####",
    "......###+#.",
    ".....###+#..",
    "....###+#...",
    "...###o#....",
    "..###o#.....",
    ".o###o......",
    "oo#o#.......",
    "o#o.o.......",
    ".o.........."};
constexpr Rows kMine = {
    "............",
    ".....+......",
    ".....#......",
    "............",
    "....oooo....",
    "...o####o...",
    "..o##++###o.",
    ".o###+#####o",
    ".o#########o",
    "oooooooooooo",
    ".oooooooooo.",
    "............"};
constexpr Rows kTesla = {
    ".......###..",
    "......###...",
    ".....###....",
    "....###.....",
    "...########.",
    ".......###..",
    "......###...",
    ".....###....",
    "....###.....",
    "....##......",
    "...##.......",
    "...#........"};
constexpr Rows kBlades = {
    ".....##.....",
    ".....##.....",
    "....####....",
    "....####....",
    "##.######.##",
    "#####oo#####",
    "#####oo#####",
    "##.######.##",
    "....####....",
    "....####....",
    ".....##.....",
    ".....##....."};

constexpr Rows kDamage = {
    ".....##.....",
    "..#..##..#..",
    "...#.##.#...",
    "....####....",
    "#...####...#",
    ".####oo####.",
    ".####oo####.",
    "#...####...#",
    "....####....",
    "...#.##.#...",
    "..#..##..#..",
    ".....##....."};
constexpr Rows kFireRate = {
    "#...#...#...",
    "##..##..##..",
    ".##..##..##.",
    "..##..##..##",
    "...#...#...#",
    "...#...#...#",
    "..##..##..##",
    ".##..##..##.",
    "##..##..##..",
    "#...#...#...",
    "............",
    "............"};
constexpr Rows kArea = {
    "...######...",
    "..#......#..",
    ".#..####..#.",
    "#..#....#..#",
    "#.#..##..#.#",
    "#.#.#++#.#.#",
    "#.#.#++#.#.#",
    "#.#..##..#.#",
    "#..#....#..#",
    ".#..####..#.",
    "..#......#..",
    "...######..."};
constexpr Rows kMoveSpeed = {
    "............",
    "......#.....",
    ".......#....",
    "..######.#..",
    ".........##.",
    ".###########",
    ".........##.",
    "..######.#..",
    ".......#....",
    "......#.....",
    "............",
    "............"};
constexpr Rows kMagnet = {
    "............",
    ".###....###.",
    ".###....###.",
    ".+++....+++.",
    ".###....###.",
    ".###....###.",
    ".###....###.",
    ".####..####.",
    "..########..",
    "...######...",
    "....####....",
    "............"};
constexpr Rows kArmor = {
    ".##########.",
    ".#########o.",
    ".####+####o.",
    ".####+####o.",
    ".#++++++++o.",
    ".####+####o.",
    ".####+####o.",
    "..###+###o..",
    "..####+##o..",
    "...######...",
    "....####....",
    "............"};

constexpr Rows kHeal = {
    "............",
    "....####....",
    "....####....",
    "....####....",
    ".##########.",
    ".##########.",
    ".##########.",
    ".##########.",
    "....####....",
    "....####....",
    "....####....",
    "............"};
constexpr Rows kBomb = {
    ".........#..",
    "........#.+.",
    ".......#....",
    "....oooo#...",
    "...oo####o..",
    "..oo##++##o.",
    "..o###+###o.",
    "..o#######o.",
    "..o#######o.",
    "...o#####o..",
    "....ooooo...",
    "............"};
constexpr Rows kAmmo = {
    ".oooooooooo.",
    ".o########o.",
    ".o#+#+#+##o.",
    ".o#o#o#o##o.",
    ".o########o.",
    ".o#+#+#+##o.",
    ".o#o#o#o##o.",
    ".o########o.",
    ".oooooooooo.",
    "............",
    "............",
    "............"};
constexpr Rows kGem = {
    "............",
    "...######...",
    "..#++#####..",
    ".#++#######.",
    "############",
    ".####oo####.",
    "..###oo###..",
    "...##oo##...",
    "....#oo#....",
    ".....##.....",
    "............",
    "............"};

// The zombie head on the title screen. It has its own colors.
constexpr Rows kZombie = {
    "...######...",
    "..########..",
    ".##########.",
    ".#ee####ee#.",
    ".#ee####ee#.",
    ".##########.",
    ".####oo####.",
    ".##########.",
    ".#mbmbmbmb#.",
    "..#mmmmmm#..",
    "..oo####oo..",
    "...oo##oo..."};
// clang-format on

Color shade(Color c) {
    return Color{static_cast<unsigned char>(c.r * 0.5f), static_cast<unsigned char>(c.g * 0.5f), static_cast<unsigned char>(c.b * 0.5f), c.a};
}

Color light(Color c) {
    return Color{static_cast<unsigned char>(c.r + (255 - c.r) * 0.55f), static_cast<unsigned char>(c.g + (255 - c.g) * 0.55f),
                 static_cast<unsigned char>(c.b + (255 - c.b) * 0.55f), c.a};
}

// Draws a grid centered on cx, cy. One square is a whole number of screen pixels.
void drawGrid(const char* const* rows, float cx, float cy, float size, Color main) {
    const int cell = std::max(1, static_cast<int>(size / static_cast<float>(kGrid)));
    const int left = static_cast<int>(std::lround(cx)) - cell * kGrid / 2;
    const int top = static_cast<int>(std::lround(cy)) - cell * kGrid / 2;
    const Color colors[3] = {main, light(main), shade(main)};
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            const char c = rows[y][x];
            if (c == '.') continue;
            const Color color = c == '#' ? colors[0] : (c == '+' ? colors[1] : colors[2]);
            DrawRectangle(left + x * cell, top + y * cell, cell, cell, color);
        }
    }
}

}  // namespace

void drawWeaponIcon(WeaponId id, float cx, float cy, float size, Color color) {
    const char* const* rows = nullptr;
    switch (id) {
        case WeaponId::Rifle: rows = kRifle; break;
        case WeaponId::Shotgun: rows = kShotgun; break;
        case WeaponId::Smg: rows = kSmg; break;
        case WeaponId::Sniper: rows = kSniper; break;
        case WeaponId::Flamethrower: rows = kFlamethrower; break;
        case WeaponId::GrenadeLauncher: rows = kGrenade; break;
        case WeaponId::Rockets: rows = kRocket; break;
        case WeaponId::Landmines: rows = kMine; break;
        case WeaponId::Tesla: rows = kTesla; break;
        case WeaponId::Blades: rows = kBlades; break;
        case WeaponId::Count: break;
    }
    if (rows) drawGrid(rows, cx, cy, size, color);
}

void drawPassiveIcon(PassiveId id, float cx, float cy, float size, Color color) {
    const char* const* rows = nullptr;
    switch (id) {
        case PassiveId::Damage: rows = kDamage; break;
        case PassiveId::FireRate: rows = kFireRate; break;
        case PassiveId::Area: rows = kArea; break;
        case PassiveId::MoveSpeed: rows = kMoveSpeed; break;
        case PassiveId::PickupRange: rows = kMagnet; break;
        case PassiveId::Armor: rows = kArmor; break;
        case PassiveId::Count: break;
    }
    if (rows) drawGrid(rows, cx, cy, size, color);
}

void drawPickupIcon(PickupType type, float cx, float cy, float size, Color color) {
    const char* const* rows = nullptr;
    switch (type) {
        case PickupType::Heal: rows = kHeal; break;
        case PickupType::Magnet: rows = kMagnet; break;
        case PickupType::Bomb: rows = kBomb; break;
        case PickupType::Ammo: rows = kAmmo; break;
        case PickupType::Count: break;
    }
    if (rows) drawGrid(rows, cx, cy, size, color);
}

void drawGemIcon(float cx, float cy, float size, Color color) { drawGrid(kGem, cx, cy, size, color); }

void drawZombieIcon(float cx, float cy, float size, float alpha) {
    const int cell = std::max(1, static_cast<int>(size / static_cast<float>(kGrid)));
    const int left = static_cast<int>(std::lround(cx)) - cell * kGrid / 2;
    const int top = static_cast<int>(std::lround(cy)) - cell * kGrid / 2;
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            Color color;
            switch (kZombie[y][x]) {
                case '#': color = theme::kZombieSkin; break;
                case 'o': color = theme::kZombieDark; break;
                case 'e': color = theme::kUiHealth; break;
                case 'm': color = theme::kUiBorderDark; break;
                case 'b': color = theme::kUiText; break;
                default: continue;
            }
            color.a = static_cast<unsigned char>(static_cast<float>(color.a) * alpha);
            DrawRectangle(left + x * cell, top + y * cell, cell, cell, color);
        }
    }
}

}  // namespace ls::ui
