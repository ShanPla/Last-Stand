#include "components/WorldView/Sprites.h"
#include "core/Math.h"

#include <algorithm>
#include <cmath>

// Every zombie is a set of body parts on a simple skeleton. A pose only moves the
// joints, and the same painter draws the parts, so each type's silhouette and gait
// come from a few numbers. Units are art pixels at normal size, y up from the feet.
namespace ls::worldview {

namespace {

constexpr Ramp kSkin{kPSkin, 4};
constexpr Ramp kRot{kPRot, 2};
constexpr Ramp kHair{kPHair, 3};
constexpr Ramp kShirt{kPShirt, 4};
constexpr Ramp kPants{kPPants, 4};
constexpr Ramp kShoes{kPShoes, 2};
constexpr Ramp kBlood{kPBlood, 3};
constexpr Ramp kMetal{kPMetal, 4};
constexpr Ramp kBone{kPBone, 4};
constexpr Ramp kBile{kPBile, 4};

// The skeleton. Side view uses x as forward. Front and back views use x as sideways.
struct Body {
    Vec2 hip, chest, head;
    Vec2 nearFoot, farFoot;
    Vec2 nearElbow, farElbow;
    Vec2 nearHand, farHand;
    float headRx = 6.0f, headRy = 5.4f;
    float hipR = 4.0f, chestR = 4.6f;
    float legTop = 2.2f, legBottom = 1.6f;
    float armTop = 1.6f, armBottom = 1.2f;
    float handR = 1.5f;
    float shoeLength = 2.5f;
    float kneeBend = 1.2f;
    bool armsInFront = true;  // Back views hide the hands behind the body.
};

// A foot goes back along the ground while planted, then forward through the air.
Vec2 footPath(float phase, float offsetX, float stride, float lift) {
    const float angle = phase * kTau;
    return {offsetX + stride * std::cos(angle), lift * std::max(0.0f, -std::sin(angle))};
}

// Walking toward or away from the camera, a foot moves up and down the screen.
Vec2 footPathFacing(float phase, float offsetX, float stride, float lift) {
    const float angle = phase * kTau;
    return {offsetX, lift * std::max(0.0f, -std::sin(angle)) + stride * 0.35f * (0.5f + 0.5f * std::cos(angle))};
}

void paintLeg(Canvas& c, const Body& b, Vec2 hip, Vec2 foot, int shift) {
    const Vec2 knee{(hip.x + foot.x) * 0.5f + b.kneeBend, (hip.y + foot.y) * 0.5f + 0.5f};
    const float mid = (b.legTop + b.legBottom) * 0.5f;
    c.limb(hip.x, hip.y, knee.x, knee.y, b.legTop, mid, kPants, shift);
    c.limb(knee.x, knee.y, foot.x, foot.y + 1.2f, mid, b.legBottom, kPants, shift);
    c.limb(foot.x - 0.3f, foot.y + 0.9f, foot.x + b.shoeLength, foot.y + 0.8f, 1.1f, 0.9f, kShoes, shift);
}

void paintFrontLeg(Canvas& c, const Body& b, Vec2 hip, Vec2 foot, int shift) {
    const float mid = (b.legTop + b.legBottom) * 0.5f;
    const Vec2 knee{(hip.x + foot.x) * 0.5f, (hip.y + foot.y) * 0.5f};
    c.limb(hip.x, hip.y, knee.x, knee.y, b.legTop, mid, kPants, shift);
    c.limb(knee.x, knee.y, foot.x, foot.y + 1.2f, mid, b.legBottom, kPants, shift);
    c.ball(foot.x, foot.y + 0.8f, b.legBottom + 0.6f, 1.2f, kShoes, shift);
}

void paintArm(Canvas& c, const Body& b, Vec2 shoulder, Vec2 elbow, Vec2 hand, int shift) {
    c.limb(shoulder.x, shoulder.y, elbow.x, elbow.y, b.armTop, (b.armTop + b.armBottom) * 0.5f, kShirt, shift);
    c.limb(elbow.x, elbow.y, hand.x, hand.y, (b.armTop + b.armBottom) * 0.5f, b.armBottom, kSkin, shift);
    c.ball(hand.x, hand.y, b.handR, b.handR, kSkin, shift);
}

// Hair is only a cap at the top and back of the skull, so the face has room to read.
void paintHairSide(Canvas& c, Vec2 head, float rx, float ry) {
    c.ball(head.x - rx * 0.30f, head.y + ry * 0.62f, rx * 0.74f, ry * 0.48f, kHair);
    c.ball(head.x - rx * 0.66f, head.y + ry * 0.18f, rx * 0.34f, ry * 0.46f, kHair);
}

void paintFaceSide(Canvas& c, Vec2 head, float rx, float ry) {
    // A hollow eye, a nose, and a jaw hanging open.
    const float eyeX = head.x + rx * 0.22f;
    const float eyeY = head.y + ry * 0.36f;
    c.patch(eyeX, eyeY, 3.0f, 2.0f, kPSocket);
    c.dot(eyeX + 1.5f, eyeY - 0.6f, kPGlint);
    c.dot(head.x + rx * 0.98f, head.y - ry * 0.05f, kPSkin + 1);
    // Lower jaw, dropped below the skull.
    c.ball(head.x + rx * 0.26f, head.y - ry * 0.92f, rx * 0.66f, ry * 0.36f, kSkin, -1);
    c.patch(head.x + rx * 0.05f, head.y - ry * 0.30f, rx * 0.92f, ry * 0.50f, kPMouth);
    c.dot(head.x + rx * 0.35f, head.y - ry * 0.34f, kPTeeth);
    c.dot(head.x + rx * 0.72f, head.y - ry * 0.34f, kPTeeth);
    c.dot(head.x + rx * 0.55f, head.y - ry * 0.70f, kPTeeth);
}

void paintFaceFront(Canvas& c, Vec2 head, float rx, float ry) {
    const float eyeY = head.y + ry * 0.32f;
    c.patch(head.x - rx * 0.62f, eyeY, 2.0f, 2.0f, kPSocket);
    c.patch(head.x + rx * 0.20f, eyeY, 2.0f, 2.0f, kPSocket);
    c.dot(head.x - rx * 0.42f, eyeY - 0.6f, kPGlint);
    c.dot(head.x + rx * 0.40f, eyeY - 0.6f, kPGlint);
    c.dot(head.x, head.y - ry * 0.05f, kPRot);
    // Jaw hanging open, with the top teeth showing.
    c.ball(head.x, head.y - ry * 0.86f, rx * 0.66f, ry * 0.36f, kSkin, -1);
    c.patch(head.x - rx * 0.45f, head.y - ry * 0.30f, rx * 0.95f, ry * 0.58f, kPMouth);
    c.dot(head.x - rx * 0.25f, head.y - ry * 0.34f, kPTeeth);
    c.dot(head.x + rx * 0.20f, head.y - ry * 0.34f, kPTeeth);
}

// Holes and stains. Fixed positions relative to the chest, so they stay on the
// clothes through the walk cycle instead of crawling around.
void paintTorsoWear(Canvas& c, Vec2 chest, Vec2 hip, float width) {
    const Vec2 mid = lerp(hip, chest, 0.45f);
    c.patch(mid.x + width * 0.25f, mid.y + 2.0f, 2.0f, 2.0f, kPSkin + 1);
    c.dot(mid.x + width * 0.35f, mid.y + 1.0f, kPBlood + 1);
    c.dot(mid.x - width * 0.45f, mid.y + 3.2f, kPBlood + 2);
    c.patch(mid.x - width * 0.30f, mid.y - 1.0f, 2.0f, 1.0f, kPBlood + 1);
    c.dot(chest.x - width * 0.10f, chest.y + 2.0f, kPShirt);
    c.dot(chest.x + width * 0.45f, chest.y - 0.5f, kPShirt);
    // A ragged hem: dark notches along the bottom of the shirt.
    const float hemY = hip.y - width * 0.72f;
    for (int i = -2; i <= 2; ++i) {
        const float x = hip.x + static_cast<float>(i) * width * 0.34f;
        if (i % 2 == 0) c.dot(x, hemY, kPShirt);
        else c.erase(x, hemY - 1.0f);
    }
}

void paintSide(Canvas& c, const Body& b) {
    // Far limbs first and one tone darker, so depth reads with no extra shapes.
    paintArm(c, b, Vec2{b.chest.x + 0.5f, b.chest.y + 2.0f}, b.farElbow, b.farHand, -1);
    paintLeg(c, b, Vec2{b.hip.x + 0.6f, b.hip.y}, b.farFoot, -1);
    paintLeg(c, b, Vec2{b.hip.x - 0.6f, b.hip.y}, b.nearFoot, 0);
    c.limb(b.hip.x, b.hip.y + 0.5f, b.chest.x, b.chest.y, b.hipR, b.chestR, kShirt);
    paintTorsoWear(c, b.chest, b.hip, b.chestR);
    // Neck, then the skull with its hair pushed back from the face.
    c.limb(b.chest.x + 0.5f, b.chest.y + 2.0f, b.head.x, b.head.y - 2.0f, 1.8f, 1.6f, kSkin, -1);
    c.ball(b.head.x, b.head.y, b.headRx, b.headRy, kSkin);
    paintHairSide(c, b.head, b.headRx, b.headRy);
    c.dot(b.head.x - b.headRx * 0.75f, b.head.y - b.headRy * 0.35f, kPRot + 1);
    paintFaceSide(c, b.head, b.headRx, b.headRy);
    c.setContour(true);
    paintArm(c, b, Vec2{b.chest.x - 0.5f, b.chest.y + 1.5f}, b.nearElbow, b.nearHand, 0);
    c.setContour(false);
}

void paintFront(Canvas& c, const Body& b) {
    const Vec2 leftHip{b.hip.x - b.hipR * 0.5f, b.hip.y};
    const Vec2 rightHip{b.hip.x + b.hipR * 0.5f, b.hip.y};
    paintFrontLeg(c, b, leftHip, b.farFoot, 0);
    paintFrontLeg(c, b, rightHip, b.nearFoot, 0);
    if (!b.armsInFront) {
        paintArm(c, b, Vec2{b.chest.x - b.chestR, b.chest.y + 1.0f}, b.farElbow, b.farHand, -1);
        paintArm(c, b, Vec2{b.chest.x + b.chestR, b.chest.y + 1.0f}, b.nearElbow, b.nearHand, -1);
    }
    c.limb(b.hip.x, b.hip.y + 0.5f, b.chest.x, b.chest.y, b.hipR, b.chestR, kShirt);
    paintTorsoWear(c, b.chest, b.hip, b.chestR);
    c.limb(b.chest.x, b.chest.y + 2.0f, b.head.x, b.head.y - 2.0f, 2.0f, 1.8f, kSkin, -1);
    c.ball(b.head.x, b.head.y, b.headRx, b.headRy, kSkin);
    if (b.armsInFront) {
        // Hair sits on top of the skull, and the face is below it.
        c.ball(b.head.x, b.head.y + b.headRy * 0.62f, b.headRx * 0.92f, b.headRy * 0.46f, kHair);
        paintFaceFront(c, b.head, b.headRx, b.headRy);
        c.setContour(true);
        paintArm(c, b, Vec2{b.chest.x - b.chestR, b.chest.y + 1.0f}, b.farElbow, b.farHand, 0);
        paintArm(c, b, Vec2{b.chest.x + b.chestR, b.chest.y + 1.0f}, b.nearElbow, b.nearHand, 0);
        c.setContour(false);
    } else {
        // From behind: all hair, ears, and a tear down the back of the shirt.
        c.ball(b.head.x, b.head.y + b.headRy * 0.15f, b.headRx * 0.96f, b.headRy * 0.84f, kHair);
        c.dot(b.head.x - b.headRx, b.head.y - 0.5f, kPSkin);
        c.dot(b.head.x + b.headRx - 1.0f, b.head.y - 0.5f, kPSkin);
        c.patch(b.chest.x - 1.0f, b.chest.y + 1.0f, 2.0f, 4.0f, kPSkin + 1);
        c.dot(b.chest.x, b.chest.y - 3.0f, kPBlood + 1);
    }
}

// --- Walker: a slow shamble with the head lolling to one side -----------------

Body walkerBody(int facing, float t) {
    Body b;
    const float angle = t * kTau;
    const float dip = 0.9f * (0.5f + 0.5f * std::sin(angle));
    const float sway = 0.9f * std::sin(angle);
    if (facing == kFacingSide) {
        b.hip = {-0.5f + sway * 0.3f, 12.0f - dip};
        b.chest = {1.2f + sway * 0.6f, 17.5f - dip};
        b.head = {4.2f + sway, 24.0f - dip};
        b.nearFoot = footPath(t, -0.5f, 5.0f, 2.6f);
        b.farFoot = footPath(t + 0.5f, 0.5f, 3.0f, 1.2f);  // It drags this leg.
        b.nearElbow = {6.0f, 15.5f + sway * 0.5f - dip};
        b.nearHand = {11.5f, 15.5f + sway - dip};
        b.farElbow = {6.5f, 14.0f - sway * 0.4f - dip};
        b.farHand = {12.0f, 13.5f - sway - dip};
    } else {
        const float lean = facing == kFacingFront ? 1.0f : -0.6f;
        b.hip = {sway * 0.5f, 12.0f - dip};
        b.chest = {sway * 0.8f, 17.5f - dip};
        b.head = {sway * 1.6f + lean, 24.0f - dip};
        b.hipR = 4.4f;
        b.chestR = 5.2f;
        b.nearFoot = footPathFacing(t, 2.6f, 3.0f, 2.2f);
        b.farFoot = footPathFacing(t + 0.5f, -2.6f, 2.0f, 1.2f);
        if (facing == kFacingFront) {
            // Both arms reach out toward the camera.
            b.farElbow = {-7.0f, 17.0f - sway * 0.4f - dip};
            b.farHand = {-5.0f, 20.0f - sway - dip};
            b.nearElbow = {7.0f, 17.0f + sway * 0.4f - dip};
            b.nearHand = {5.5f, 20.5f + sway - dip};
        } else {
            b.armsInFront = false;
            b.farElbow = {-7.5f, 15.0f - dip};
            b.farHand = {-8.0f, 18.0f + sway - dip};
            b.nearElbow = {7.5f, 15.0f - dip};
            b.nearHand = {8.0f, 18.0f - sway - dip};
        }
    }
    return b;
}

// --- Runner: small, falling forward, legs going fast --------------------------

Body runnerBody(int facing, float t) {
    Body b;
    const float angle = t * kTau;
    const float bob = 0.8f * std::abs(std::sin(angle));
    const float pump = 3.0f * std::sin(angle);
    b.headRx = 4.4f;
    b.headRy = 4.0f;
    b.hipR = 2.8f;
    b.chestR = 3.3f;
    b.legTop = 1.8f;
    b.legBottom = 1.1f;
    b.armTop = 1.2f;
    b.armBottom = 0.9f;
    b.handR = 1.1f;
    b.shoeLength = 2.0f;
    b.kneeBend = 2.2f;
    if (facing == kFacingSide) {
        b.hip = {-3.5f, 9.5f + bob};
        b.chest = {2.5f, 13.0f + bob};
        b.head = {8.0f, 15.0f + bob};
        b.nearFoot = footPath(t, -3.0f, 7.0f, 5.5f);
        b.farFoot = footPath(t + 0.5f, -3.0f, 7.0f, 5.5f);
        b.nearElbow = {4.5f + pump * 0.4f, 9.0f + bob};
        b.nearHand = {9.0f + pump, 9.5f + bob};
        b.farElbow = {0.0f - pump * 0.4f, 10.5f + bob};
        b.farHand = {3.5f - pump, 8.0f + bob};
    } else {
        b.hip = {0.0f, 9.5f + bob};
        b.chest = {0.0f, 13.0f + bob};
        b.head = {0.0f, facing == kFacingFront ? 16.5f + bob : 18.0f + bob};
        b.hipR = 3.0f;
        b.chestR = 3.8f;
        b.nearFoot = footPathFacing(t, 2.0f, 5.0f, 4.5f);
        b.farFoot = footPathFacing(t + 0.5f, -2.0f, 5.0f, 4.5f);
        if (facing == kFacingFront) {
            b.farElbow = {-5.5f, 11.0f + bob};
            b.farHand = {-4.5f, 12.0f + pump * 0.6f + bob};
            b.nearElbow = {5.5f, 11.0f + bob};
            b.nearHand = {4.5f, 12.0f - pump * 0.6f + bob};
        } else {
            b.armsInFront = false;
            b.farElbow = {-5.5f, 11.0f + bob};
            b.farHand = {-5.0f, 9.0f + pump * 0.6f + bob};
            b.nearElbow = {5.5f, 11.0f + bob};
            b.nearHand = {5.0f, 9.0f - pump * 0.6f + bob};
        }
    }
    return b;
}

// --- Brute: huge shoulders, tiny head, a heavy stomp --------------------------

void paintBrute(Canvas& c, int facing, float t) {
    const float angle = t * kTau;
    // The weight rises slowly, then drops hard as a foot lands.
    const float wave = std::abs(std::sin(angle));
    const float y = 2.0f * (1.0f - (1.0f - wave) * (1.0f - wave)) - 1.0f;
    const float swing = 2.0f * std::sin(angle);
    Body b;
    b.headRx = 3.8f;
    b.headRy = 3.6f;
    b.legTop = 4.4f;
    b.legBottom = 3.2f;
    b.armTop = 4.0f;
    b.armBottom = 3.2f;
    b.handR = 3.8f;
    b.shoeLength = 3.0f;
    b.kneeBend = 0.8f;
    if (facing == kFacingSide) {
        const Vec2 hip{-1.0f, 13.0f + y};
        const Vec2 chest{1.0f, 24.0f + y};
        const Vec2 head{5.5f, 33.5f + y};
        const Vec2 near = footPath(t, -0.5f, 4.0f, 3.2f);
        const Vec2 far = footPath(t + 0.5f, 0.5f, 4.0f, 3.2f);
        const Vec2 shoulderFar{3.0f, 28.0f + y};
        paintArm(c, b, shoulderFar, Vec2{7.0f + swing * 0.5f, 19.0f + y}, Vec2{8.0f + swing, 11.0f + y}, -1);
        paintLeg(c, b, Vec2{hip.x + 1.5f, hip.y}, far, -1);
        paintLeg(c, b, Vec2{hip.x - 1.5f, hip.y}, near, 0);
        c.ball(0.0f, 16.0f + y, 8.0f, 7.0f, kPants);  // Heavy hips.
        c.limb(hip.x, hip.y + 6.0f, chest.x, chest.y, 8.5f, 11.0f, kShirt);
        c.ball(chest.x + 1.0f, chest.y + 4.0f, 11.0f, 7.0f, kShirt);  // The hump of the shoulders.
        c.patch(2.0f, 24.0f + y, 5.0f, 3.0f, kPSkin + 1);
        c.dot(4.0f, 22.0f + y, kPBlood + 1);
        c.patch(-4.0f, 19.0f + y, 3.0f, 2.0f, kPBlood + 1);
        c.dot(-5.0f, 26.0f + y, kPSkin + 1);
        c.ball(head.x, head.y, b.headRx, b.headRy, kSkin);
        paintHairSide(c, head, b.headRx, b.headRy);
        paintFaceSide(c, head, b.headRx, b.headRy);
        c.setContour(true);
        const Vec2 shoulderNear{-6.0f, 27.5f + y};
        c.ball(shoulderNear.x, shoulderNear.y, 5.6f, 5.4f, kShirt);
        c.limb(shoulderNear.x, shoulderNear.y - 2.0f, -8.5f - swing * 0.6f, 17.0f + y, 3.6f, 3.2f, kSkin);
        c.limb(-8.5f - swing * 0.6f, 17.0f + y, -6.5f - swing, 9.0f + y, 3.2f, 2.8f, kSkin);
        c.ball(-6.5f - swing, 8.0f + y, 3.4f, 3.4f, kSkin);
        c.setContour(false);
        c.dot(-9.0f - swing * 0.6f, 15.0f + y, kPRot);
        c.dot(-5.0f - swing, 6.5f + y, kPOutline);
        c.dot(-7.0f - swing, 6.0f + y, kPOutline);
    } else {
        const bool front = facing == kFacingFront;
        const Vec2 near = footPathFacing(t, 6.5f, 3.0f, 3.2f);
        const Vec2 far = footPathFacing(t + 0.5f, -6.5f, 3.0f, 3.2f);
        b.hipR = 8.0f;
        paintFrontLeg(c, b, Vec2{-5.5f, 13.0f + y}, far, 0);
        paintFrontLeg(c, b, Vec2{5.5f, 13.0f + y}, near, 0);
        c.ball(0.0f, 15.0f + y, 9.5f, 6.0f, kPants);
        c.limb(0.0f, 18.0f + y, 0.0f, 25.0f + y, 10.0f, 12.0f, kShirt);
        c.ball(0.0f, 29.0f + y, 15.0f, 6.0f, kShirt);
        if (front) {
            c.patch(-3.0f, 27.0f + y, 6.0f, 5.0f, kPSkin + 1);
            c.dot(-4.0f, 20.0f + y, kPBlood + 1);
            c.patch(3.0f, 21.0f + y, 3.0f, 2.0f, kPBlood + 1);
        } else {
            c.patch(-6.0f, 26.0f + y, 3.0f, 8.0f, kPSkin + 1);
            c.patch(3.0f, 24.0f + y, 3.0f, 2.0f, kPBlood);
        }
        c.ball(0.0f, 33.5f + y, b.headRx, b.headRy, kSkin);
        if (front) {
            c.ball(0.0f, 35.5f + y, b.headRx * 0.9f, b.headRy * 0.46f, kHair);
            paintFaceFront(c, Vec2{0.0f, 33.5f + y}, b.headRx, b.headRy);
        } else {
            c.ball(0.0f, 34.0f + y, b.headRx * 0.96f, b.headRy * 0.84f, kHair);
        }
        c.setContour(true);
        const float armDrop = front ? 0.0f : 0.0f;
        c.limb(-15.0f, 28.0f + y, -17.0f, 18.0f + y + swing * 0.5f + armDrop, 4.4f, 3.8f, kSkin);
        c.limb(-17.0f, 18.0f + y + swing * 0.5f, -15.5f, 10.0f + y + swing, 3.8f, 3.2f, kSkin);
        c.ball(-15.5f, 9.0f + y + swing, 3.8f, 3.8f, kSkin);
        c.limb(15.0f, 28.0f + y, 17.0f, 18.0f + y - swing * 0.5f + armDrop, 4.4f, 3.8f, kSkin);
        c.limb(17.0f, 18.0f + y - swing * 0.5f, 15.5f, 10.0f + y - swing, 3.8f, 3.2f, kSkin);
        c.ball(15.5f, 9.0f + y - swing, 3.8f, 3.8f, kSkin);
        c.ball(-14.5f, 29.0f + y, 5.5f, 5.0f, kShirt);
        c.ball(14.5f, 29.0f + y, 5.5f, 5.0f, kShirt);
        c.setContour(false);
    }
}

// --- Spitter: tall, thin, with a swollen throat -------------------------------

void paintSpitter(Canvas& c, int facing, int pose, float t) {
    Body b;
    const float angle = t * kTau;
    const float sway = 0.8f * std::sin(angle);
    const bool inflate = pose == kPoseSpitInflate;
    const bool recoil = pose == kPoseSpitRecoil;
    const float swell = inflate ? 7.0f : (recoil ? 4.0f : 5.0f);
    // It rears back to fill up, then snaps forward as it spits.
    const float lean = inflate ? -2.5f : (recoil ? 3.0f : 0.5f);
    b.headRx = 4.4f;
    b.headRy = 4.2f;
    b.hipR = 2.8f;
    b.chestR = 3.0f;
    b.legTop = 1.6f;
    b.legBottom = 1.1f;
    b.armTop = 1.2f;
    b.armBottom = 0.9f;
    b.handR = 1.2f;
    b.kneeBend = 1.0f;
    if (facing == kFacingSide) {
        b.hip = {-0.5f, 15.5f};
        b.chest = {0.5f + sway * 0.4f + lean * 0.4f, 23.0f};
        b.head = {2.0f + sway + lean, 35.0f - (inflate ? 0.0f : 0.5f)};
        b.nearFoot = footPath(t, -0.5f, 4.0f, 2.2f);
        b.farFoot = footPath(t + 0.5f, 0.5f, 4.0f, 2.2f);
        b.nearElbow = {1.0f - sway, 17.0f};
        b.nearHand = {3.0f - sway * 1.5f, 11.0f};
        b.farElbow = {2.5f + sway, 17.0f};
        b.farHand = {5.0f + sway * 1.5f, 11.5f};
        paintArm(c, b, Vec2{b.chest.x, b.chest.y + 1.5f}, b.farElbow, b.farHand, -1);
        paintLeg(c, b, Vec2{b.hip.x + 0.5f, b.hip.y}, b.farFoot, -1);
        paintLeg(c, b, Vec2{b.hip.x - 0.5f, b.hip.y}, b.nearFoot, 0);
        c.limb(b.hip.x, b.hip.y + 0.5f, b.chest.x, b.chest.y, b.hipR, b.chestR, kShirt);
        paintTorsoWear(c, b.chest, b.hip, b.chestR);
        c.ball(b.chest.x + 1.5f + lean * 0.5f, 29.5f, swell, swell * 0.86f, kBile);
        c.dot(b.chest.x + 3.0f + lean * 0.5f, 31.0f, kPBile + 3);
        c.dot(b.chest.x + 0.0f + lean * 0.5f, 27.5f, kPBile);
        c.ball(b.head.x, b.head.y, b.headRx, b.headRy, kSkin);
        paintHairSide(c, b.head, b.headRx, b.headRy);
        paintFaceSide(c, b.head, b.headRx, b.headRy);
        c.setContour(true);
        paintArm(c, b, Vec2{b.chest.x - 0.5f, b.chest.y + 1.0f}, b.nearElbow, b.nearHand, 0);
        c.setContour(false);
    } else {
        const bool front = facing == kFacingFront;
        b.hip = {sway * 0.4f, 15.5f};
        b.chest = {sway * 0.7f, 23.0f};
        b.head = {sway * 1.4f, 35.0f};
        b.hipR = 3.0f;
        b.chestR = 3.4f;
        b.nearFoot = footPathFacing(t, 2.0f, 3.0f, 2.0f);
        b.farFoot = footPathFacing(t + 0.5f, -2.0f, 3.0f, 2.0f);
        b.armsInFront = false;
        b.farElbow = {-5.5f, 17.0f};
        b.farHand = {-6.0f + sway, 11.0f};
        b.nearElbow = {5.5f, 17.0f};
        b.nearHand = {6.0f + sway, 11.0f};
        const Vec2 leftHip{b.hip.x - 1.6f, b.hip.y};
        const Vec2 rightHip{b.hip.x + 1.6f, b.hip.y};
        paintFrontLeg(c, b, leftHip, b.farFoot, 0);
        paintFrontLeg(c, b, rightHip, b.nearFoot, 0);
        paintArm(c, b, Vec2{b.chest.x - b.chestR, b.chest.y + 1.0f}, b.farElbow, b.farHand, -1);
        paintArm(c, b, Vec2{b.chest.x + b.chestR, b.chest.y + 1.0f}, b.nearElbow, b.nearHand, -1);
        c.limb(b.hip.x, b.hip.y + 0.5f, b.chest.x, b.chest.y, b.hipR, b.chestR, kShirt);
        paintTorsoWear(c, b.chest, b.hip, b.chestR);
        c.ball(b.chest.x, 29.5f, swell, swell * 0.9f, kBile);
        c.dot(b.chest.x + 2.0f, 31.0f, kPBile + 3);
        c.dot(b.chest.x - 2.0f, 27.5f, kPBile);
        c.dot(b.chest.x + 1.0f, 27.0f, kPBile);
        c.ball(b.head.x, b.head.y, b.headRx, b.headRy, kSkin);
        if (front) {
            c.ball(b.head.x, b.head.y + b.headRy * 0.62f, b.headRx * 0.92f, b.headRy * 0.46f, kHair);
            paintFaceFront(c, b.head, b.headRx, b.headRy);
        } else {
            c.ball(b.head.x, b.head.y + b.headRy * 0.15f, b.headRx * 0.96f, b.headRy * 0.84f, kHair);
        }
    }
}

// --- Bloater: a huge round belly on small legs, wobbling ----------------------

void paintBloater(Canvas& c, int facing, float t) {
    const float angle = t * kTau;
    const float wobble = std::sin(angle * 2.0f);
    const float rx = 14.0f + 0.9f * wobble;
    const float ry = 12.5f - 0.8f * wobble;
    const float bellyY = 14.5f - 0.6f * wobble;
    Body b;
    b.legTop = 3.2f;
    b.legBottom = 2.6f;
    b.shoeLength = 2.5f;
    b.armTop = 2.2f;
    b.armBottom = 1.6f;
    b.handR = 1.8f;
    const float headY = bellyY + ry + 2.0f;
    if (facing == kFacingSide) {
        b.kneeBend = 0.4f;
        paintLeg(c, b, Vec2{0.5f, 6.0f}, footPath(t + 0.5f, 1.0f, 3.0f, 1.8f), -1);
        paintLeg(c, b, Vec2{-1.0f, 6.0f}, footPath(t, -1.0f, 3.0f, 1.8f), 0);
        c.ball(1.5f, bellyY, rx, ry, kSkin);
        // A shirt stretched over the top, then giving up over the belly.
        c.ball(0.0f, bellyY + ry * 0.45f, rx * 0.8f, ry * 0.58f, kShirt);
        c.patch(-5.0f, bellyY + ry * 0.05f, 3.0f, 1.0f, kPShirt);
        c.patch(3.0f, bellyY + ry * 0.0f, 2.0f, 1.0f, kPShirt);
        // Boils that glow a little: the type's signature, seen from any side.
        c.ball(8.0f, bellyY - 2.0f, 2.2f, 2.2f, kBile, 1);
        c.ball(4.0f, bellyY - 6.5f, 1.6f, 1.6f, kBile, 1);
        c.ball(-3.0f, bellyY - 3.5f, 1.4f, 1.4f, kBile);
        c.patch(10.0f, bellyY + 3.0f, 2.0f, 2.0f, kPRot);
        c.ball(5.0f, headY, 4.2f, 4.0f, kSkin);
        paintHairSide(c, Vec2{5.0f, headY}, 4.2f, 4.0f);
        paintFaceSide(c, Vec2{5.0f, headY}, 4.2f, 4.0f);
        c.setContour(true);
        const float swing = wobble * 1.0f;
        paintArm(c, b, Vec2{-4.0f, bellyY + ry * 0.62f}, Vec2{-8.0f, bellyY + 3.0f}, Vec2{-7.0f + swing, bellyY - 2.0f + swing}, 0);
        c.setContour(false);
    } else {
        const bool front = facing == kFacingFront;
        b.hipR = 7.0f;
        paintFrontLeg(c, b, Vec2{-6.0f, 7.0f}, footPathFacing(t + 0.5f, -6.5f, 2.0f, 2.0f), 0);
        paintFrontLeg(c, b, Vec2{6.0f, 7.0f}, footPathFacing(t, 6.5f, 2.0f, 2.0f), 0);
        c.ball(0.0f, bellyY, rx + 1.5f, ry, kSkin);
        c.ball(0.0f, bellyY + ry * 0.48f, rx * 0.86f, ry * 0.52f, kShirt);
        if (front) {
            c.ball(-6.0f, bellyY - 3.0f, 2.0f, 2.0f, kBile, 1);
            c.ball(5.0f, bellyY - 6.0f, 2.4f, 2.4f, kBile, 1);
            c.ball(1.0f, bellyY - 2.0f, 1.3f, 1.3f, kBile);
            c.dot(0.0f, bellyY - 8.0f, kPRot);
            c.dot(0.0f, bellyY - 7.0f, kPRot);
        } else {
            c.ball(5.0f, bellyY + 2.0f, 1.8f, 1.8f, kBile, 1);
            c.patch(-4.0f, bellyY + 7.0f, 2.0f, 5.0f, kPSkin + 1);
        }
        c.ball(0.0f, headY, 4.4f, 4.0f, kSkin);
        if (front) {
            c.ball(0.0f, headY + 2.2f, 4.0f, 2.0f, kHair);
            paintFaceFront(c, Vec2{0.0f, headY}, 4.4f, 4.0f);
        } else {
            c.ball(0.0f, headY + 0.5f, 4.2f, 3.4f, kHair);
        }
        c.setContour(true);
        const float swing = wobble * 1.2f;
        paintArm(c, b, Vec2{-rx + 1.0f, bellyY + 5.0f}, Vec2{-rx - 2.0f, bellyY + 1.0f}, Vec2{-rx - 1.0f, bellyY - 3.5f - swing}, 0);
        paintArm(c, b, Vec2{rx - 1.0f, bellyY + 5.0f}, Vec2{rx + 2.0f, bellyY + 1.0f}, Vec2{rx + 1.0f, bellyY - 3.5f + swing}, 0);
        c.setContour(false);
    }
}

// --- Crawler: low on all fours, dragging its legs -----------------------------

void paintCrawler(Canvas& c, int facing, float t) {
    const float angle = t * kTau;
    const float reach = std::sin(angle);
    const float reachOther = std::sin(angle + kPi);
    Body b;
    b.armTop = 1.5f;
    b.armBottom = 1.1f;
    b.handR = 1.4f;
    if (facing == kFacingSide) {
        const float bob = 0.5f * std::abs(reach);
        // Legs trail behind, dragged and limp.
        c.limb(-8.0f, 4.0f, -13.5f, 1.8f + reach * 0.3f, 2.0f, 1.3f, kPants, -1);
        c.limb(-13.5f, 1.8f + reach * 0.3f, -19.0f, 1.2f, 1.3f, 1.0f, kPants, -1);
        c.limb(-8.0f, 4.5f, -13.0f, 2.2f - reach * 0.3f, 2.2f, 1.5f, kPants);
        c.limb(-13.0f, 2.2f - reach * 0.3f, -18.0f, 1.8f, 1.5f, 1.2f, kShoes);
        paintArm(c, b, Vec2{4.0f, 7.5f + bob}, Vec2{9.0f + reachOther * 2.0f, 5.0f + bob}, Vec2{12.5f + reachOther * 3.5f, 1.2f}, -1);
        c.limb(-7.0f, 5.5f + bob, 3.5f, 7.0f + bob, 3.6f, 4.0f, kShirt);
        c.patch(-3.0f, 9.5f + bob, 3.0f, 2.0f, kPSkin + 1);
        c.dot(0.0f, 8.0f + bob, kPBlood + 1);
        c.dot(-5.0f, 7.0f + bob, kPBlood + 2);
        const Vec2 head{8.5f, 6.5f + bob};
        c.ball(head.x, head.y, 4.6f, 4.2f, kSkin);
        paintHairSide(c, head, 4.6f, 4.2f);
        paintFaceSide(c, head, 4.6f, 4.2f);
        c.setContour(true);
        paintArm(c, b, Vec2{3.0f, 7.5f + bob}, Vec2{7.5f + reach * 2.0f, 4.0f + bob}, Vec2{11.0f + reach * 3.5f, 1.2f}, 0);
        c.setContour(false);
    } else if (facing == kFacingFront) {
        const float bob = 0.5f * std::abs(reach);
        c.ball(0.0f, 9.5f + bob, 6.8f, 4.5f, kShirt);  // The hunched back rises behind the head.
        c.patch(-3.0f, 11.5f + bob, 3.0f, 1.0f, kPBlood + 1);
        const Vec2 head{0.0f, 6.5f + bob};
        c.ball(head.x, head.y, 5.0f, 4.6f, kSkin);
        c.ball(head.x, head.y + 2.6f, 4.4f, 2.2f, kHair);
        paintFaceFront(c, head, 5.0f, 4.6f);
        c.setContour(true);
        paintArm(c, b, Vec2{-6.0f, 7.5f + bob}, Vec2{-9.5f, 4.5f + bob}, Vec2{-8.5f, 1.0f + reach * 1.0f}, 0);
        paintArm(c, b, Vec2{6.0f, 7.5f + bob}, Vec2{9.5f, 4.5f + bob}, Vec2{8.5f, 1.0f + reachOther * 1.0f}, 0);
        c.setContour(false);
    } else {
        const float bob = 0.5f * std::abs(reach);
        c.ball(0.0f, 10.5f + bob, 3.5f, 3.0f, kHair);  // The head, past the back.
        paintArm(c, b, Vec2{-5.0f, 8.0f + bob}, Vec2{-8.0f, 5.0f + bob}, Vec2{-8.5f, 2.0f + reach}, -1);
        paintArm(c, b, Vec2{5.0f, 8.0f + bob}, Vec2{8.0f, 5.0f + bob}, Vec2{8.5f, 2.0f + reachOther}, -1);
        c.ball(0.0f, 7.5f + bob, 6.5f, 5.0f, kShirt);
        c.patch(-1.0f, 10.0f + bob, 2.0f, 4.0f, kPSkin + 1);
        c.ball(0.0f, 3.5f, 5.5f, 3.2f, kPants);
        c.limb(-3.0f, 2.5f, -4.0f + reach * 0.5f, 0.5f, 1.8f, 1.3f, kPants);
        c.limb(3.0f, 2.5f, 4.0f - reach * 0.5f, 0.5f, 1.8f, 1.3f, kPants);
    }
}

// --- Charger: an armored, horned front, built to ram --------------------------

void paintHorns(Canvas& c, Vec2 head, float rx, float ry, bool sideView) {
    if (sideView) {
        c.limb(head.x + rx * 0.2f, head.y + ry * 0.6f, head.x + rx + 3.0f, head.y + ry + 2.5f, 1.3f, 0.5f, kBone);
        c.limb(head.x - rx * 0.4f, head.y + ry * 0.6f, head.x + rx - 1.0f, head.y + ry + 4.0f, 1.2f, 0.5f, kBone, -1);
    } else {
        c.limb(head.x - rx * 0.6f, head.y + ry * 0.5f, head.x - rx - 2.5f, head.y + ry + 3.0f, 1.3f, 0.5f, kBone);
        c.limb(head.x + rx * 0.6f, head.y + ry * 0.5f, head.x + rx + 2.5f, head.y + ry + 3.0f, 1.3f, 0.5f, kBone);
    }
}

void paintCharger(Canvas& c, int facing, int pose, float t) {
    const bool brace = pose == kPoseBraceA || pose == kPoseBraceB;
    const bool dash = pose == kPoseDashA || pose == kPoseDashB;
    const bool recover = pose == kPoseRecover;
    Body b;
    b.headRx = 4.8f;
    b.headRy = 4.4f;
    b.hipR = 3.8f;
    b.chestR = 5.8f;
    b.legTop = 2.8f;
    b.legBottom = 2.0f;
    b.armTop = 2.2f;
    b.armBottom = 2.0f;
    b.handR = 2.2f;
    b.shoeLength = 3.0f;

    if (facing == kFacingSide) {
        // The joints move a long way between poses, so the lean reads at a glance.
        Vec2 hip{-1.5f, 12.0f};
        Vec2 chest{3.0f, 19.0f};
        Vec2 head{8.5f, 22.5f};
        Vec2 near = footPath(t, -1.0f, 4.5f, 3.0f);
        Vec2 far = footPath(t + 0.5f, 0.0f, 4.5f, 3.0f);
        Vec2 nearHand{10.5f, 9.5f};
        Vec2 farHand{13.0f, 11.5f};
        if (brace) {
            const float shake = pose == kPoseBraceA ? -0.5f : 0.5f;
            hip = {-4.0f + shake, 8.5f};
            chest = {1.0f + shake, 15.0f};
            head = {7.0f + shake, 16.5f};
            near = {-7.0f, 0.0f};
            far = {4.5f, 0.0f};
            nearHand = {5.5f + shake, 3.0f};
            farHand = {8.5f + shake, 3.5f};
        } else if (dash) {
            const float kick = pose == kPoseDashA ? 1.0f : -1.0f;
            hip = {-7.0f, 10.0f};
            chest = {3.0f, 13.5f};
            head = {11.0f, 13.0f};
            near = {-11.0f + 6.0f * kick, 1.0f + (kick > 0.0f ? 3.0f : 0.0f)};
            far = {-11.0f - 6.0f * kick, 1.0f + (kick < 0.0f ? 3.0f : 0.0f)};
            nearHand = {-3.0f, 9.0f};
            farHand = {-5.5f, 10.5f};
        } else if (recover) {
            hip = {-0.5f, 11.0f};
            chest = {1.0f, 18.0f};
            head = {4.5f, 21.0f};
            near = {-3.0f, 0.0f};
            far = {3.0f, 0.0f};
            nearHand = {4.0f, 5.0f};
            farHand = {6.5f, 5.0f};
        }
        const Vec2 farShoulder{chest.x + 1.0f, chest.y + 2.0f};
        const Vec2 nearShoulder{chest.x - 1.0f, chest.y + 2.0f};
        paintArm(c, b, farShoulder, lerp(farShoulder, farHand, 0.5f) + Vec2{1.0f, -0.5f}, farHand, -1);
        paintLeg(c, b, Vec2{hip.x + 0.8f, hip.y}, far, -1);
        paintLeg(c, b, Vec2{hip.x - 0.8f, hip.y}, near, 0);
        c.limb(hip.x, hip.y + 0.5f, chest.x, chest.y, b.hipR, b.chestR, kShirt);
        // Scrap-metal armor on the chest and shoulder: its front is unmistakable.
        const float dirX = chest.x - hip.x;
        const float dirY = chest.y - hip.y;
        const float len = std::max(0.1f, std::sqrt(dirX * dirX + dirY * dirY));
        const Vec2 front{chest.x + dirY / len * 2.5f, chest.y - dirX / len * 2.5f};
        c.setContour(true);
        c.limb(front.x, front.y + 2.0f, front.x - dirX * 0.45f, front.y - dirY * 0.45f - 1.0f, 4.0f, 3.0f, kMetal);
        c.setContour(false);
        c.dot(front.x + 1.0f, front.y + 3.0f, kPMetal + 3);
        c.dot(front.x - 1.0f, front.y - 2.0f, kPMetal);
        c.ball(head.x, head.y, b.headRx, b.headRy, kSkin);
        paintHairSide(c, head, b.headRx, b.headRy);
        paintFaceSide(c, head, b.headRx, b.headRy);
        paintHorns(c, head, b.headRx, b.headRy, true);
        // A metal plate on the skull, the last thing to hit you.
        c.patch(head.x + 0.5f, head.y + b.headRy * 0.72f, 4.0f, 2.0f, kPMetal + 2);
        c.setContour(true);
        c.ball(nearShoulder.x, nearShoulder.y, 3.5f, 3.0f, kMetal);
        c.limb(nearShoulder.x, nearShoulder.y - 1.0f, nearHand.x * 0.5f + nearShoulder.x * 0.5f + 1.0f, (nearShoulder.y + nearHand.y) * 0.5f - 0.5f, 2.2f, 2.0f, kSkin);
        c.limb(nearHand.x * 0.5f + nearShoulder.x * 0.5f + 1.0f, (nearShoulder.y + nearHand.y) * 0.5f - 0.5f, nearHand.x, nearHand.y, 2.0f, 1.9f, kSkin);
        c.ball(nearHand.x, nearHand.y, b.handR, b.handR, kSkin);
        c.setContour(false);
    } else {
        const bool front = facing == kFacingFront;
        const float crouch = brace ? 4.0f : (dash ? 6.0f : (recover ? 2.0f : 0.0f));
        const float shake = brace ? (pose == kPoseBraceA ? -0.6f : 0.6f) : 0.0f;
        const float hipY = 12.0f - crouch * 0.6f;
        const float chestY = 19.0f - crouch;
        const float headY = (front ? 24.0f : 27.0f) - crouch * 1.3f;
        const Vec2 near = (brace || recover) ? Vec2{5.0f, 0.0f} : footPathFacing(t, 3.5f, 3.0f, 2.5f);
        const Vec2 far = (brace || recover) ? Vec2{-5.0f, 0.0f} : footPathFacing(t + 0.5f, -3.5f, 3.0f, 2.5f);
        paintFrontLeg(c, b, Vec2{-3.0f + shake, hipY}, far, 0);
        paintFrontLeg(c, b, Vec2{3.0f + shake, hipY}, near, 0);
        c.limb(shake, hipY + 0.5f, shake, chestY, b.hipR, b.chestR + 0.5f, kShirt);
        if (front) {
            c.setContour(true);
            c.box(-5.0f + shake, chestY - 4.0f, 5.0f + shake, chestY + 3.5f, kMetal);
            c.setContour(false);
            c.dot(-3.0f + shake, chestY + 2.0f, kPMetal + 3);
            c.dot(3.0f + shake, chestY - 3.0f, kPMetal);
        } else {
            c.patch(-1.0f + shake, chestY + 2.0f, 2.0f, 5.0f, kPSkin + 1);
            c.dot(2.0f + shake, chestY - 1.0f, kPBlood + 1);
        }
        const Vec2 head{shake, headY};
        c.ball(head.x, head.y, b.headRx, b.headRy, kSkin);
        if (front) {
            c.ball(head.x, head.y + b.headRy * 0.62f, b.headRx * 0.92f, b.headRy * 0.46f, kHair);
            paintFaceFront(c, head, b.headRx, b.headRy);
            c.patch(head.x - 2.0f, head.y + b.headRy * 0.95f, 4.0f, 2.0f, kPMetal + 2);
        } else {
            c.ball(head.x, head.y + b.headRy * 0.15f, b.headRx * 0.96f, b.headRy * 0.84f, kHair);
        }
        paintHorns(c, head, b.headRx, b.headRy, false);
        c.setContour(true);
        const float handY = brace ? 3.0f : (dash ? 9.0f : (recover ? 5.0f : 10.0f));
        const float handX = dash ? 9.0f : 10.0f;
        paintArm(c, b, Vec2{-7.0f + shake, chestY + 2.0f}, Vec2{-9.5f + shake, chestY - 3.0f}, Vec2{-handX + shake, handY}, 0);
        paintArm(c, b, Vec2{7.0f + shake, chestY + 2.0f}, Vec2{9.5f + shake, chestY - 3.0f}, Vec2{handX + shake, handY}, 0);
        c.ball(-7.0f + shake, chestY + 2.5f, 3.5f, 3.0f, kMetal);
        c.ball(7.0f + shake, chestY + 2.5f, 3.5f, 3.0f, kMetal);
        c.setContour(false);
    }
}

// --- Boss: about three times a Brute, with a different look each phase ---------

void paintBossArmHuge(Canvas& c, Vec2 shoulder, Vec2 elbow, Vec2 hand, float r, int shift) {
    c.limb(shoulder.x, shoulder.y, elbow.x, elbow.y, r * 1.2f, r, kSkin, shift);
    c.limb(elbow.x, elbow.y, hand.x, hand.y, r, r * 0.8f, kSkin, shift);
    c.ball(hand.x, hand.y - 1.0f, r * 1.05f, r, kSkin, shift);
    // Claws.
    for (int i = -1; i <= 1; ++i) {
        const float x = hand.x + static_cast<float>(i) * r * 0.7f;
        c.limb(x, hand.y - r * 0.6f, x + static_cast<float>(i), hand.y - r * 1.8f, 1.6f, 0.5f, kBone, shift);
    }
}

void paintBossDetails(Canvas& c, int phase, Vec2 chest, float y, bool front) {
    if (phase == 0) {
        // Exposed ribs and torn overalls.
        for (int i = 0; i < 4; ++i) {
            const float rowY = chest.y + 7.0f - static_cast<float>(i) * 4.0f;
            c.limb(chest.x - 9.0f, rowY + 1.0f, chest.x + 9.0f, rowY + 1.0f, 0.9f, 0.9f, kBone);
        }
        c.ball(chest.x, chest.y - 16.0f + y, 7.0f, 4.0f, kBlood);
        c.ball(chest.x - 1.0f, chest.y - 15.5f + y, 3.5f, 1.8f, Ramp{kPMouth, 1});
        c.box(chest.x - 12.0f, chest.y + 2.0f, chest.x - 9.0f, chest.y + 15.0f, kPants);
        c.box(chest.x + 9.0f, chest.y + 2.0f, chest.x + 12.0f, chest.y + 15.0f, kPants);
    } else if (phase == 1) {
        // Bolted scrap plates, with burning cracks between them.
        c.setContour(true);
        c.box(chest.x - 15.0f, chest.y - 2.0f, chest.x - 1.5f, chest.y + 13.0f, kMetal);
        c.box(chest.x + 1.5f, chest.y - 2.0f, chest.x + 15.0f, chest.y + 13.0f, kMetal);
        c.box(chest.x - 12.0f, chest.y - 17.0f, chest.x + 12.0f, chest.y - 5.0f, kMetal);
        c.setContour(false);
        c.limb(chest.x - 13.0f, chest.y - 3.5f, chest.x + 13.0f, chest.y - 3.5f, 1.2f, 1.2f, kBile, 1);
        c.limb(chest.x, chest.y - 3.0f, chest.x, chest.y + 14.0f, 1.0f, 1.0f, kBile, 1);
        for (int i = 0; i < 4; ++i) {
            c.dot(chest.x - 13.0f + static_cast<float>(i) * 9.0f, chest.y + 11.0f, kPMetal + 3);
            c.dot(chest.x - 13.0f + static_cast<float>(i) * 9.0f, chest.y - 0.5f, kPMetal);
        }
    } else {
        // Flayed: glowing heart and burning veins.
        c.ball(chest.x, chest.y + 3.0f, 8.0f, 8.0f, kBile);
        c.ball(chest.x - 1.0f, chest.y + 4.0f, 4.5f, 4.5f, kBile, 2);
        c.limb(chest.x, chest.y - 4.0f, chest.x - 6.0f, chest.y - 19.0f, 1.2f, 0.7f, kBile, 1);
        c.limb(chest.x + 2.0f, chest.y - 4.0f, chest.x + 7.0f, chest.y - 18.0f, 1.2f, 0.7f, kBile, 1);
        c.limb(chest.x - 7.0f, chest.y + 2.0f, chest.x - 16.0f, chest.y + 9.0f, 1.0f, 0.6f, kBile, 1);
        c.limb(chest.x + 7.0f, chest.y + 2.0f, chest.x + 16.0f, chest.y + 9.0f, 1.0f, 0.6f, kBile, 1);
    }
    (void)front;
}

void paintBossSpikes(Canvas& c, int phase, float shoulderY, float headX, float headY) {
    if (phase == 1) {
        // Rebar through the shoulders.
        c.limb(-20.0f, shoulderY + 2.0f, -29.0f, shoulderY + 20.0f, 1.3f, 1.0f, kMetal);
        c.limb(-14.0f, shoulderY + 4.0f, -18.0f, shoulderY + 26.0f, 1.3f, 1.0f, kMetal);
        c.limb(20.0f, shoulderY + 2.0f, 27.0f, shoulderY + 22.0f, 1.3f, 1.0f, kMetal);
        c.limb(13.0f, shoulderY + 4.0f, 18.0f, shoulderY + 26.0f, 1.3f, 1.0f, kMetal);
    } else if (phase == 2) {
        // Bone spikes bursting out of the back and head.
        for (int i = -3; i <= 3; ++i) {
            const float x = static_cast<float>(i) * 8.0f;
            const float tall = 14.0f + 5.0f * (3.0f - std::abs(static_cast<float>(i)));
            c.limb(x, shoulderY + 3.0f, x * 1.25f, shoulderY + tall, 2.4f, 0.5f, kBone);
        }
        c.limb(headX - 6.0f, headY + 8.0f, headX - 12.0f, headY + 20.0f, 2.0f, 0.5f, kBone);
        c.limb(headX + 6.0f, headY + 8.0f, headX + 12.0f, headY + 20.0f, 2.0f, 0.5f, kBone);
    }
}

void paintBoss(Canvas& c, int phase, int facing, int pose, float t) {
    const bool charge = pose == kPoseBraceA || pose == kPoseBraceB;
    const bool dash = pose == kPoseDashA;
    const float angle = t * kTau;
    const float swing = 6.0f * std::sin(angle);
    float bob = 2.0f * std::abs(std::sin(angle)) - 1.0f;
    if (charge) bob = pose == kPoseBraceA ? -5.0f : -6.5f;
    if (dash) bob = -3.0f;
    const float raise = charge ? (pose == kPoseBraceA ? 22.0f : 26.0f) : 0.0f;

    if (facing == kFacingSide) {
        const float lean = dash ? 14.0f : 0.0f;
        const Vec2 hip{-3.0f, 36.0f + bob};
        const Vec2 chest{2.0f + lean, 62.0f + bob};
        const Vec2 head{10.0f + lean * 1.6f, 87.0f + bob - (dash ? 8.0f : 0.0f)};
        const Vec2 nearFoot = footPath(t, -4.0f, 10.0f, 7.0f);
        const Vec2 farFoot = footPath(t + 0.5f, 2.0f, 10.0f, 7.0f);
        Body legs;
        legs.legTop = 10.5f;
        legs.legBottom = 7.5f;
        legs.shoeLength = 6.0f;
        legs.kneeBend = 2.0f;
        paintBossArmHuge(c, Vec2{chest.x + 8.0f, chest.y + 12.0f}, Vec2{chest.x + 20.0f, chest.y - 8.0f + raise}, Vec2{chest.x + 22.0f + swing * 0.5f, chest.y - 32.0f + raise * 2.0f}, 8.0f, -1);
        paintLeg(c, legs, Vec2{hip.x + 4.0f, hip.y}, farFoot, -1);
        paintLeg(c, legs, Vec2{hip.x - 4.0f, hip.y}, nearFoot, 0);
        c.ball(hip.x, hip.y + 2.0f, 18.0f, 13.0f, kPants);
        c.limb(hip.x, hip.y + 10.0f, chest.x, chest.y, 18.0f, 24.0f, kShirt);
        c.ball(chest.x + 2.0f, chest.y + 10.0f, 25.0f, 15.0f, kShirt);
        paintBossDetails(c, phase, Vec2{chest.x + 3.0f, chest.y + 0.0f}, 0.0f, false);
        paintBossSpikes(c, phase, chest.y + 16.0f, head.x, head.y);
        c.ball(head.x, head.y, 11.0f, 10.5f, kSkin);
        c.ball(head.x - 3.0f, head.y + 5.0f, 8.5f, 6.0f, kHair);
        // Burning eye, and a jaw that could take a head off.
        c.ball(head.x + 5.0f, head.y + 2.5f, 2.8f, 2.4f, kBile, 2);
        c.ball(head.x + 3.0f, head.y - 7.5f, 8.0f, 4.0f, kSkin, -1);
        c.patch(head.x - 1.0f, head.y - 2.0f, 11.0f, 5.0f, kPMouth);
        for (int i = 0; i < 4; ++i) {
            c.patch(head.x + static_cast<float>(i) * 2.8f, head.y - 2.0f, 1.0f, 2.0f, kPTeeth);
            c.patch(head.x + 1.0f + static_cast<float>(i) * 2.6f, head.y - 5.0f, 1.0f, 2.0f, kPTeeth);
        }
        c.setContour(true);
        // The near arm hangs at the back of the body, leaving the chest's detail in view.
        const Vec2 nearShoulder{chest.x - 15.0f, chest.y + 12.0f};
        c.ball(nearShoulder.x, nearShoulder.y, 11.0f, 10.0f, phase == 1 ? kMetal : kShirt);
        paintBossArmHuge(c, nearShoulder, Vec2{nearShoulder.x - 5.0f - swing * 0.5f, chest.y - 14.0f + raise}, Vec2{nearShoulder.x - 1.0f - swing, chest.y - 38.0f + raise * 2.0f}, 7.5f, 0);
        c.setContour(false);
    } else {
        const bool front = facing == kFacingFront;
        const float crouch = dash ? 10.0f : 0.0f;
        const Vec2 chest{0.0f, 62.0f + bob - crouch};
        const Vec2 head{0.0f, (front ? 88.0f : 90.0f) + bob - crouch * 1.5f};
        Body legs;
        legs.legTop = 10.5f;
        legs.legBottom = 7.5f;
        paintFrontLeg(c, legs, Vec2{-13.0f, 34.0f + bob}, footPathFacing(t + 0.5f, -14.0f, 7.0f, 7.0f), 0);
        paintFrontLeg(c, legs, Vec2{13.0f, 34.0f + bob}, footPathFacing(t, 14.0f, 7.0f, 7.0f), 0);
        c.ball(0.0f, 37.0f + bob, 22.0f, 13.0f, kPants);
        c.limb(0.0f, 44.0f + bob, 0.0f, chest.y, 22.0f, 27.0f, kShirt);
        c.ball(0.0f, chest.y + 11.0f, 33.0f, 14.0f, kShirt);
        if (front) {
            paintBossDetails(c, phase, chest, 0.0f, true);
        } else {
            c.patch(-11.0f, chest.y + 12.0f, 5.0f, 30.0f, kPSkin + 1);
            c.patch(5.0f, chest.y + 2.0f, 4.0f, 18.0f, kPSkin + 1);
        }
        paintBossSpikes(c, phase, chest.y + 16.0f, head.x, head.y);
        c.ball(head.x, head.y, 12.0f, 10.5f, kSkin);
        if (front) {
            c.ball(head.x, head.y + 6.5f, 10.5f, 4.0f, kHair);
            c.ball(head.x - 5.0f, head.y + 2.0f, 2.8f, 2.4f, kBile, 2);
            c.ball(head.x + 5.0f, head.y + 2.0f, 2.8f, 2.4f, kBile, 2);
            c.ball(head.x, head.y - 7.5f, 9.0f, 4.0f, kSkin, -1);
            c.patch(head.x - 7.0f, head.y - 2.5f, 14.0f, 6.0f, kPMouth);
            for (int i = 0; i < 6; ++i) {
                c.patch(head.x - 6.5f + static_cast<float>(i) * 2.5f, head.y - 2.5f, 1.0f, 2.0f, kPTeeth);
                c.patch(head.x - 5.5f + static_cast<float>(i) * 2.2f, head.y - 6.0f, 1.0f, 2.0f, kPTeeth);
            }
        } else {
            c.ball(head.x, head.y + 1.5f, 11.0f, 8.5f, kHair);
        }
        c.setContour(true);
        const float armOut = 36.0f;
        paintBossArmHuge(c, Vec2{-armOut + 6.0f, chest.y + 11.0f}, Vec2{-armOut - 5.0f, chest.y - 12.0f + raise * 0.8f + swing * 0.4f}, Vec2{-armOut - 2.0f, chest.y - 36.0f + raise * 2.2f + swing}, 9.5f, 0);
        paintBossArmHuge(c, Vec2{armOut - 6.0f, chest.y + 11.0f}, Vec2{armOut + 5.0f, chest.y - 12.0f + raise * 0.8f - swing * 0.4f}, Vec2{armOut + 2.0f, chest.y - 36.0f + raise * 2.2f - swing}, 9.5f, 0);
        c.ball(-armOut + 7.0f, chest.y + 11.0f, 13.0f, 11.0f, phase == 1 ? kMetal : kShirt);
        c.ball(armOut - 7.0f, chest.y + 11.0f, 13.0f, 11.0f, phase == 1 ? kMetal : kShirt);
        c.setContour(false);
    }
}

void paintFigureBody(Canvas& c, Figure figure, int facing, int pose, float t) {
    switch (figure) {
        case Figure::Walker: {
            const Body b = walkerBody(facing, t);
            if (facing == kFacingSide) paintSide(c, b);
            else paintFront(c, b);
            break;
        }
        case Figure::Runner: {
            const Body b = runnerBody(facing, t);
            if (facing == kFacingSide) paintSide(c, b);
            else paintFront(c, b);
            break;
        }
        case Figure::Brute: paintBrute(c, facing, t); break;
        case Figure::Spitter: paintSpitter(c, facing, pose, t); break;
        case Figure::Bloater: paintBloater(c, facing, t); break;
        case Figure::Crawler: paintCrawler(c, facing, t); break;
        case Figure::Charger: paintCharger(c, facing, pose, t); break;
        case Figure::Boss1: paintBoss(c, 0, facing, pose, t); break;
        case Figure::Boss2: paintBoss(c, 1, facing, pose, t); break;
        case Figure::Boss3: paintBoss(c, 2, facing, pose, t); break;
        case Figure::Count: break;
    }
}

}  // namespace

void paintFigure(Canvas& c, Figure figure, int facing, int pose) {
    const bool boss = figure >= Figure::Boss1;
    const float t = static_cast<float>(pose) / static_cast<float>(boss ? kBossWalkFrames : kWalkFrames);
    paintFigureBody(c, figure, facing, pose, t);
    c.outline(kPOutline);
}

void paintCorpse(Canvas& c, Figure figure, int frame) {
    // A falling corpse is a walking pose, turned around its feet. The last frame
    // adds a pool of blood underneath, so the pile of dead reads as a battle scar.
    static constexpr float kTurns[kCorpseFrames] = {0.45f, 1.05f, 1.48f};
    const float height = figure >= Figure::Boss1 ? 40.0f : (figure == Figure::Brute || figure == Figure::Bloater ? 14.0f : 8.0f);
    if (frame == kCorpseFrames - 1) {
        c.ball(-height * 1.1f, 2.0f, height * 1.4f, height * 0.45f, kBlood);
    }
    const float turn = figure == Figure::Crawler ? kTurns[frame] * 0.15f : kTurns[frame];
    c.setTurn(turn);
    paintFigureBody(c, figure, kFacingSide, 0, 0.25f);
    c.clearFrame();
    c.outline(kPOutline);
}

}  // namespace ls::worldview
