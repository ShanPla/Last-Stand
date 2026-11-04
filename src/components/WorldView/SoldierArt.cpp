#include "components/WorldView/Sprites.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>

// The soldier is painted without arms. His arms and gun are one separate sprite,
// baked at several angles, so the gun can point anywhere while the legs keep walking.
namespace ls::worldview {

namespace {

constexpr Ramp kHelmet{kPHelmet, 4};
constexpr Ramp kVest{kPVest, 4};
constexpr Ramp kPantsOlive{kPSoldierPants, 4};
constexpr Ramp kSleeve{kPSoldierPants, 4};  // His shirt is the same olive as his trousers.
constexpr Ramp kBoots{kPBoots, 3};
constexpr Ramp kGloves{kPGloves, 2};
constexpr Ramp kFace{kPFace, 4};
constexpr Ramp kStrap{kPStrap, 2};
constexpr Ramp kGun{kPGun, 3};
constexpr Ramp kWood{kPWood, 2};

Vec2 walkFoot(float phase, float offsetX, float stride, float lift) {
    const float angle = phase * kTau;
    return {offsetX + stride * std::cos(angle), lift * std::max(0.0f, -std::sin(angle))};
}

void paintLegSide(Canvas& c, Vec2 hip, Vec2 foot, int shift) {
    const Vec2 knee{(hip.x + foot.x) * 0.5f + 1.2f, (hip.y + foot.y) * 0.5f + 0.5f};
    c.limb(hip.x, hip.y, knee.x, knee.y, 2.5f, 2.1f, kPantsOlive, shift);
    c.limb(knee.x, knee.y, foot.x, foot.y + 2.5f, 2.1f, 1.8f, kPantsOlive, shift);
    // A boot: tall ankle and a thick toe.
    c.limb(foot.x, foot.y + 2.6f, foot.x, foot.y + 1.2f, 2.0f, 1.8f, kBoots, shift);
    c.limb(foot.x - 0.5f, foot.y + 1.0f, foot.x + 3.2f, foot.y + 0.9f, 1.2f, 1.0f, kBoots, shift);
    c.dot(knee.x + 1.0f, knee.y, kPSoldierPants);
}

// head is the middle of the face. The helmet sits above it, so the face stays in
// view: a big round helmet over a visible face is what makes him read as a soldier.
void paintHelmetSide(Canvas& c, float x, float y) {
    c.ball(x, y + 6.5f, 7.6f, 5.8f, kHelmet);
    // Camouflage marks, kept away from the highlight so the dome still looks round.
    c.patch(x - 3.0f, y + 10.5f, 2.0f, 2.0f, kPCamo);
    c.patch(x + 2.0f, y + 8.0f, 3.0f, 1.0f, kPCamo);
    c.patch(x - 6.0f, y + 7.0f, 2.0f, 2.0f, kPCamo + 1);
    c.patch(x + 1.0f, y + 11.5f, 2.0f, 1.0f, kPCamo + 1);
    c.dot(x - 1.0f, y + 6.5f, kPCamo);
    // The brim sticks out over the eyes.
    c.limb(x - 7.2f, y + 2.0f, x + 8.2f, y + 2.4f, 0.9f, 0.9f, kHelmet, -1);
}

void paintHeadSide(Canvas& c, Vec2 head) {
    c.ball(head.x + 2.0f, head.y - 0.2f, 5.0f, 4.2f, kFace);
    c.patch(head.x + 4.0f, head.y + 0.4f, 1.0f, 2.0f, kPOutline);  // Eye.
    c.dot(head.x + 6.6f, head.y - 0.2f, kPFace + 3);               // Nose.
    c.patch(head.x + 3.5f, head.y - 2.5f, 2.0f, 1.0f, kPFace);     // Mouth.
    c.patch(head.x - 0.5f, head.y + 1.0f, 1.0f, 4.0f, kPStrap);    // Chin strap.
    paintHelmetSide(c, head.x, head.y);
}

void paintSide(Canvas& c, float t, bool moving, float idle) {
    const float bob = moving ? 0.7f * std::abs(std::sin(t * kTau)) : idle;
    const Vec2 hip{-0.5f, 11.5f + bob};
    const Vec2 chest{0.5f, 18.0f + bob};
    const Vec2 head{1.5f, 24.0f + bob};
    const Vec2 near = moving ? walkFoot(t, -0.5f, 5.5f, 3.2f) : Vec2{-2.0f, 0.0f};
    const Vec2 far = moving ? walkFoot(t + 0.5f, 0.5f, 5.5f, 3.2f) : Vec2{2.5f, 0.0f};
    paintLegSide(c, Vec2{hip.x + 0.8f, hip.y}, far, -1);
    paintLegSide(c, Vec2{hip.x - 0.8f, hip.y}, near, 0);
    // Pack on the back.
    c.setContour(true);
    c.box(-8.0f, 13.0f + bob, -3.0f, 22.5f + bob, kHelmet);
    c.setContour(false);
    c.patch(-7.0f, 21.5f + bob, 3.0f, 1.0f, kPStrap);
    c.patch(-7.0f, 18.0f + bob, 3.0f, 2.0f, kPHelmet);
    // Vest over the body.
    c.limb(hip.x, hip.y + 2.0f, chest.x, chest.y, 4.2f, 5.0f, kVest);
    c.patch(0.0f, 16.5f + bob, 4.0f, 3.0f, kPVest);        // Pouch.
    c.patch(1.0f, 16.0f + bob, 2.0f, 1.0f, kPVest + 3);
    c.patch(-3.0f, 21.0f + bob, 1.0f, 5.0f, kPStrap);      // Shoulder strap.
    c.patch(-4.0f, 11.5f + bob, 9.0f, 2.0f, kPStrap);      // Belt.
    c.dot(3.0f, 12.0f + bob, kPBrass);
    paintHeadSide(c, head);
}

void paintFront(Canvas& c, float t, bool moving, float idle) {
    const float bob = moving ? 0.7f * std::abs(std::sin(t * kTau)) : idle;
    const float stepA = moving ? std::max(0.0f, -std::sin(t * kTau)) * 3.0f : 0.0f;
    const float stepB = moving ? std::max(0.0f, std::sin(t * kTau)) * 3.0f : 0.0f;
    const float hipY = 11.5f + bob;
    for (int side = -1; side <= 1; side += 2) {
        const float x = static_cast<float>(side) * 2.8f;
        const float lift = side < 0 ? stepA : stepB;
        c.limb(x, hipY, x, lift + 2.5f, 2.5f, 2.0f, kPantsOlive);
        c.dot(x, lift + 7.0f, kPSoldierPants);
        c.limb(x, lift + 2.6f, x + static_cast<float>(side) * 0.3f, lift + 1.0f, 2.2f, 2.2f, kBoots);
    }
    c.limb(0.0f, hipY + 2.0f, 0.0f, 18.0f + bob, 5.0f, 6.0f, kVest);
    // Pouches on the vest, in two rows, with a strap down the middle.
    c.patch(-5.0f, 16.5f + bob, 3.0f, 3.0f, kPVest);
    c.patch(2.0f, 16.5f + bob, 3.0f, 3.0f, kPVest);
    c.patch(-5.0f, 16.5f + bob, 3.0f, 1.0f, kPVest + 3);
    c.patch(2.0f, 16.5f + bob, 3.0f, 1.0f, kPVest + 3);
    c.patch(-1.0f, 21.5f + bob, 2.0f, 9.0f, kPStrap);
    c.patch(-5.0f, 11.5f + bob, 10.0f, 2.0f, kPStrap);
    c.patch(-1.0f, 11.5f + bob, 2.0f, 2.0f, kPBrass);
    // Head: a real face under the helmet's brim.
    const float headY = 24.0f + bob;
    c.ball(0.0f, headY, 5.2f, 4.4f, kFace);
    c.patch(-3.0f, headY + 0.6f, 2.0f, 2.0f, kPOutline);
    c.patch(1.0f, headY + 0.6f, 2.0f, 2.0f, kPOutline);
    c.dot(-2.5f, headY + 0.3f, kPIcon);
    c.dot(1.5f, headY + 0.3f, kPIcon);
    c.patch(-2.0f, headY - 2.8f, 4.0f, 1.0f, kPFace);
    c.dot(-5.0f, headY - 1.0f, kPStrap);
    c.dot(4.0f, headY - 1.0f, kPStrap);
    c.ball(0.0f, headY + 6.5f, 8.0f, 5.8f, kHelmet);
    c.patch(-4.0f, headY + 10.5f, 2.0f, 2.0f, kPCamo);
    c.patch(2.0f, headY + 9.0f, 3.0f, 1.0f, kPCamo);
    c.patch(-6.0f, headY + 7.5f, 2.0f, 1.0f, kPCamo + 1);
    c.patch(1.0f, headY + 11.5f, 2.0f, 1.0f, kPCamo + 1);
    c.limb(-7.5f, headY + 2.2f, 7.5f, headY + 2.2f, 0.9f, 0.9f, kHelmet, -1);
}

void paintBack(Canvas& c, float t, bool moving, float idle) {
    const float bob = moving ? 0.7f * std::abs(std::sin(t * kTau)) : idle;
    const float stepA = moving ? std::max(0.0f, -std::sin(t * kTau)) * 3.0f : 0.0f;
    const float stepB = moving ? std::max(0.0f, std::sin(t * kTau)) * 3.0f : 0.0f;
    const float hipY = 11.5f + bob;
    for (int side = -1; side <= 1; side += 2) {
        const float x = static_cast<float>(side) * 2.8f;
        const float lift = side < 0 ? stepA : stepB;
        c.limb(x, hipY, x, lift + 2.5f, 2.5f, 2.0f, kPantsOlive);
        c.limb(x, lift + 2.6f, x, lift + 1.0f, 2.2f, 2.2f, kBoots);
    }
    c.limb(0.0f, hipY + 2.0f, 0.0f, 18.0f + bob, 5.0f, 6.0f, kVest);
    c.patch(-5.0f, 11.5f + bob, 10.0f, 2.0f, kPStrap);
    // The pack covers most of his back from this side.
    c.setContour(true);
    c.box(-4.5f, 12.5f + bob, 4.5f, 22.5f + bob, kHelmet);
    c.setContour(false);
    c.patch(-3.0f, 17.0f + bob, 6.0f, 1.0f, kPStrap);
    c.patch(-3.0f, 21.5f + bob, 6.0f, 1.0f, kPHelmet + 3);
    c.patch(-1.0f, 16.0f + bob, 2.0f, 2.0f, kPHelmet);
    const float headY = 24.0f + bob;
    c.ball(0.0f, headY + 5.0f, 8.0f, 7.2f, kHelmet);
    c.patch(-4.0f, headY + 9.5f, 2.0f, 2.0f, kPCamo);
    c.patch(2.0f, headY + 7.5f, 3.0f, 2.0f, kPCamo);
    c.patch(-2.0f, headY + 4.0f, 3.0f, 1.0f, kPCamo + 1);
    c.patch(3.0f, headY + 10.5f, 2.0f, 1.0f, kPCamo + 1);
    c.limb(-7.5f, headY + 0.5f, 7.5f, headY + 0.5f, 0.9f, 0.9f, kHelmet, -1);
}

// Gun parts are boxes along the barrel: u is forward, v is up. Each gun has its own
// outline, so a change of weapon can be seen in the soldier's hands.
struct GunShape {
    float gripU;   // Where the rear hand holds.
    float foreU;   // Where the front hand holds.
    float length;  // To the muzzle.
};

GunShape gunShape(WeaponId gun) {
    switch (gun) {
        case WeaponId::Shotgun: return {3.0f, 11.0f, 18.0f};
        case WeaponId::Smg: return {3.0f, 8.0f, 13.0f};
        case WeaponId::Sniper: return {3.0f, 13.0f, 26.0f};
        case WeaponId::Flamethrower: return {3.0f, 11.0f, 18.0f};
        case WeaponId::GrenadeLauncher: return {3.0f, 10.0f, 16.0f};
        case WeaponId::Rockets: return {3.0f, 10.0f, 21.0f};
        case WeaponId::Tesla: return {3.0f, 9.0f, 16.0f};
        default: return {3.0f, 12.0f, 20.0f};
    }
}

void paintGunParts(Canvas& c, WeaponId gun) {
    switch (gun) {
        case WeaponId::Shotgun:
            c.box(-2.0f, -1.5f, 3.0f, 1.0f, kWood);
            c.box(3.0f, -1.0f, 9.0f, 1.5f, kGun);
            c.box(9.0f, 0.0f, 18.0f, 1.8f, kGun);
            c.box(9.0f, -1.5f, 13.5f, 0.0f, kWood);  // The pump.
            break;
        case WeaponId::Smg:
            c.box(-1.0f, -0.5f, 3.0f, 0.8f, kGun, -1);
            c.box(2.0f, -1.2f, 9.0f, 1.5f, kGun);
            c.box(9.0f, 0.0f, 13.0f, 1.0f, kGun, -1);
            c.box(4.0f, -6.0f, 6.0f, -1.2f, kGun, -1);  // Long magazine, straight down.
            break;
        case WeaponId::Sniper:
            c.box(-3.0f, -1.5f, 3.0f, 1.0f, kWood);
            c.box(3.0f, -1.0f, 12.0f, 1.2f, kGun);
            c.box(12.0f, -0.2f, 26.0f, 0.8f, kGun);
            c.box(5.0f, 1.2f, 11.0f, 3.0f, kGun, 1);  // The scope.
            c.patch(11.0f, 3.0f, 1.0f, 2.0f, kPGunAccent);
            c.box(5.0f, -3.5f, 7.0f, -1.0f, kGun, -1);
            break;
        case WeaponId::Flamethrower:
            c.box(-3.0f, -4.0f, 4.0f, -1.0f, Ramp{kPGunTank, 1});  // Fuel tank under the gun.
            c.box(-2.0f, -1.0f, 8.0f, 1.0f, kGun);
            c.box(8.0f, -0.5f, 16.0f, 1.0f, kGun, -1);
            c.box(16.0f, -1.5f, 18.0f, 2.0f, kGun, 1);
            c.patch(17.0f, 3.0f, 1.0f, 1.0f, kPGunAccent);
            break;
        case WeaponId::GrenadeLauncher:
            c.box(-2.0f, -1.2f, 3.0f, 0.8f, kWood);
            c.box(3.0f, -2.5f, 10.0f, 2.5f, kGun);     // Fat drum.
            c.box(10.0f, -1.5f, 16.0f, 1.5f, kGun, -1);
            c.dot(5.0f, 2.0f, kPGunAccent);
            c.dot(8.0f, 2.0f, kPGunAccent);
            break;
        case WeaponId::Rockets:
            c.box(-6.0f, -2.5f, 21.0f, 2.5f, kGun);    // A long tube over the shoulder.
            c.box(-6.0f, -3.0f, -4.0f, 3.0f, kGun, -1);
            c.box(19.0f, -3.0f, 21.0f, 3.0f, kGun, 1);
            c.box(8.0f, 2.5f, 12.0f, 4.0f, kGun, -1);
            break;
        case WeaponId::Tesla:
            c.box(-1.0f, -1.2f, 4.0f, 1.2f, kGun);
            c.box(4.0f, -1.8f, 10.0f, 1.8f, kGun, 1);
            for (int i = 0; i < 3; ++i) {
                const float u = 5.0f + static_cast<float>(i) * 2.0f;
                c.patch(u, 2.0f, 1.0f, 4.0f, kPGunAccent);
            }
            c.box(10.0f, -0.5f, 14.0f, 0.5f, kGun);
            c.ball(15.0f, 0.0f, 2.0f, 2.0f, Ramp{kPGunAccent, 1});
            break;
        default:  // Rifle.
            c.box(-3.0f, -1.5f, 3.0f, 1.0f, kWood);
            c.box(3.0f, -1.0f, 12.0f, 1.2f, kGun);
            c.box(12.0f, -0.3f, 20.0f, 0.8f, kGun);
            c.box(6.0f, -4.5f, 8.5f, -1.0f, kGun, -1);  // Magazine.
            c.box(8.5f, 1.2f, 10.0f, 2.2f, kGun, -1);   // Sight.
            break;
    }
}

}  // namespace

float gunLength(WeaponId gun) { return gunShape(gun).length; }

void paintSoldierBody(Canvas& c, int facing, int pose) {
    if (pose == kSoldierDeadPose) {
        // Fallen on his back, turned around his feet, with the same paint as the walk.
        c.ball(-13.0f, 2.0f, 13.0f, 4.0f, Ramp{kPBlood, 3});
        c.setTurn(1.5f);
        paintSide(c, 0.0f, false, 0.0f);
        c.clearFrame();
        c.outline(kPOutline);
        return;
    }
    const bool moving = pose < kSoldierWalkFrames;
    const float t = static_cast<float>(pose) / static_cast<float>(kSoldierWalkFrames);
    const float idle = pose == kSoldierIdlePose ? 0.0f : -0.6f;
    if (facing == kFacingSide) paintSide(c, t, moving, idle);
    else if (facing == kFacingFront) paintFront(c, t, moving, idle);
    else paintBack(c, t, moving, idle);
    c.outline(kPOutline);
}

void paintSoldierGun(Canvas& c, WeaponId gun, float angle) {
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);
    // A gun aimed up or down the screen is seen end-on, so it looks shorter.
    const float along = 1.0f - 0.28f * std::abs(sinA);
    const GunShape shape = gunShape(gun);

    const Vec2 rearShoulder{-1.5f, -0.5f};
    const Vec2 frontShoulder{2.0f, 0.0f};
    const Vec2 grip{cosA * shape.gripU * along + sinA * 1.0f, sinA * shape.gripU * along - cosA * 1.0f};
    const Vec2 fore{cosA * shape.foreU * along + sinA * 1.0f, sinA * shape.foreU * along - cosA * 1.0f};

    // The front arm goes behind the gun, and the rear arm in front of it.
    const Vec2 frontElbow = lerp(frontShoulder, fore, 0.5f) + Vec2{0.0f, -1.5f};
    c.limb(frontShoulder.x, frontShoulder.y, frontElbow.x, frontElbow.y, 1.8f, 1.5f, kSleeve, -1);
    c.limb(frontElbow.x, frontElbow.y, fore.x, fore.y, 1.5f, 1.3f, kSleeve, -1);

    c.setFrame(0.0f, 0.0f, cosA * along, sinA * along);
    // Parts are scaled by along through the frame, so their width shrinks with it.
    // That reads well enough at this size, and keeps one code path for all guns.
    paintGunParts(c, gun);
    c.clearFrame();

    c.ball(fore.x, fore.y, 1.7f, 1.7f, kGloves);
    const Vec2 rearElbow = lerp(rearShoulder, grip, 0.5f) + Vec2{-1.0f, -2.0f};
    c.setContour(true);
    c.limb(rearShoulder.x, rearShoulder.y, rearElbow.x, rearElbow.y, 2.1f, 1.7f, kSleeve);
    c.limb(rearElbow.x, rearElbow.y, grip.x, grip.y, 1.7f, 1.4f, kSleeve);
    c.ball(grip.x, grip.y, 1.8f, 1.8f, kGloves);
    c.setContour(false);
    c.outline(kPOutline);
}

void paintSoldierReload(Canvas& c, WeaponId gun, int frame) {
    // The gun tips up across the chest while the free hand swaps the magazine.
    const float angle = 0.6f;
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);
    const GunShape shape = gunShape(gun);
    const Vec2 grip{cosA * shape.gripU, sinA * shape.gripU - 1.0f};
    const Vec2 hand = frame == 0 ? Vec2{5.0f, -6.0f} : Vec2{6.5f, -2.0f};
    c.limb(2.0f, 0.0f, hand.x - 1.0f, hand.y - 1.0f, 1.8f, 1.4f, kSleeve, -1);
    c.setFrame(0.0f, 0.0f, cosA, sinA);
    paintGunParts(c, gun);
    c.clearFrame();
    c.patch(hand.x - 1.0f, hand.y + 2.0f, 2.0f, 3.0f, kPGun);  // The magazine in his hand.
    c.ball(hand.x, hand.y, 1.7f, 1.7f, kGloves);
    c.setContour(true);
    c.limb(-1.5f, -0.5f, grip.x - 1.0f, grip.y - 2.0f, 2.1f, 1.7f, kSleeve);
    c.limb(grip.x - 1.0f, grip.y - 2.0f, grip.x, grip.y, 1.7f, 1.4f, kSleeve);
    c.ball(grip.x, grip.y, 1.8f, 1.8f, kGloves);
    c.setContour(false);
    c.outline(kPOutline);
}

}  // namespace ls::worldview
