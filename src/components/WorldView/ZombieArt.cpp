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
// One painter draws all twelve looks: three phases for each difficulty. Each difficulty
// keeps every part of the one below it and adds more on top, so all twelve share one
// skeleton, and the walk, the feet and the attack warnings line up the same on every
// level. The additions are big shapes rather than fine detail, so they still read at
// game zoom on a phone, and at night.

constexpr Ramp kGrowth{kPGrowth, 4};

// How much bigger each difficulty draws the boss, by Difficulty. Only the picture grows:
// its body in the game, and so every hit and warning, stays the same size.
constexpr float kBossArtScale[4] = {1.0f, 1.05f, 1.1f, 1.2f};

struct BossLookArt {
    int phase;   // 0 rotting, 1 scrap armor, 2 flayed.
    int horror;  // 0 for Easy, up to 3 for Hell. Each step adds parts.
};

// A cluster of boils: a big one with smaller ones around it, each with a pale head of
// pus. The offsets are fixed, so every frame of the walk shows the same cluster. flip -1
// mirrors it, so a cluster can spread away from a face instead of over it.
void paintGrowths(Canvas& c, float x, float y, float size, int count, float flip) {
    static constexpr float kSpots[5][3] = {{0.0f, 0.0f, 1.0f}, {0.95f, 0.35f, 0.7f}, {-0.85f, 0.5f, 0.62f}, {0.25f, 1.0f, 0.55f}, {-0.4f, -0.75f, 0.5f}};
    for (int i = 0; i < std::min(count, 5); ++i) {
        const float r = size * kSpots[i][2];
        const float bx = x + kSpots[i][0] * size * flip;
        const float by = y + kSpots[i][1] * size;
        c.ball(bx, by, r, r * 0.9f, kGrowth);
        const float pus = r >= 5.0f ? 3.0f : (r >= 3.0f ? 2.0f : 1.0f);
        c.patch(bx - r * 0.45f, by + r * 0.5f, pus, pus, kPGrowth + 3);
    }
}

// An extra eye: a dark socket with an ember in it, a tone dimmer than the two main eyes
// so the face keeps one clear pair to read.
void paintSmallEye(Canvas& c, float x, float y) {
    c.patch(x - 2.0f, y + 2.0f, 5.0f, 4.0f, kPSocket);
    c.patch(x - 1.0f, y + 1.0f, 3.0f, 2.0f, kPBile + 2);
}

// A second mouth torn open in the flesh: bloody lips, a dark throat and two rows of fangs.
// A tall one has its fangs along the sides, like a chest split down the middle.
void paintMaw(Canvas& c, float x, float y, float w, float h, bool tall) {
    c.ball(x, y, w * 0.5f + 2.5f, h * 0.5f + 2.5f, kBlood);
    c.ball(x, y, w * 0.5f, h * 0.5f, Ramp{kPMouth, 1});
    if (tall) {
        // Fire deep in the throat, so the split reads as a hole into the body.
        c.ball(x, y - h * 0.1f, w * 0.18f, h * 0.28f, kBile, 1);
    }
    const float edge = tall ? h : w;
    const int teeth = std::max(3, static_cast<int>(edge / (tall ? 5.0f : 3.5f)));
    const float fang = std::min(w, h) * 0.3f + 1.0f;
    for (int i = 0; i < teeth; ++i) {
        const float along = -edge * 0.5f + edge * (static_cast<float>(i) + 0.5f) / static_cast<float>(teeth);
        if (tall) {
            c.limb(x - w * 0.5f, y + along, x - w * 0.5f + fang, y + along - 0.5f, 1.3f, 0.4f, kBone);
            c.limb(x + w * 0.5f, y + along + 1.5f, x + w * 0.5f - fang, y + along + 1.0f, 1.3f, 0.4f, kBone);
        } else {
            c.limb(x + along, y + h * 0.5f, x + along + 0.5f, y + h * 0.5f - fang, 1.3f, 0.4f, kBone);
            c.limb(x + along + 1.5f, y - h * 0.5f, x + along + 1.0f, y - h * 0.5f + fang, 1.3f, 0.4f, kBone);
        }
    }
}

// A tendril: tapering pieces that curl more toward the tip, ending in a bone barb. The
// caller sways its angle with the walk, so it writhes instead of hanging still.
void paintTendril(Canvas& c, Vec2 base, float angle, float length, float curl, float r) {
    constexpr int kPieces = 6;
    Vec2 at = base;
    float heading = angle;
    for (int i = 0; i < kPieces; ++i) {
        const float k0 = static_cast<float>(i) / kPieces;
        const float k1 = static_cast<float>(i + 1) / kPieces;
        const Vec2 next = at + fromAngle(heading) * (length / kPieces);
        c.limb(at.x, at.y, next.x, next.y, r * (1.0f - 0.65f * k0), r * (1.0f - 0.65f * k1), kGrowth);
        at = next;
        heading += curl * (0.4f + k1);
    }
    const Vec2 tip = at + fromAngle(heading + curl * 2.0f) * 5.0f;
    c.limb(at.x, at.y, tip.x, tip.y, r * 0.4f, 0.4f, kBone);
}

// A glowing wound: a jagged line of the boss's warm inner fire, seen through the skin.
void paintCrack(Canvas& c, Vec2 a, Vec2 b, Vec2 d) {
    c.limb(a.x, a.y, b.x, b.y, 1.4f, 1.1f, kBile, 1);
    c.limb(b.x, b.y, d.x, d.y, 1.1f, 0.6f, kBile, 1);
}

// Hell only: a second, smaller head bursting out of the body, its mouth open.
void paintSecondHead(Canvas& c, Vec2 at) {
    c.limb(at.x, at.y - 7.0f, at.x, at.y, 4.5f, 4.0f, kSkin, -1);
    c.ball(at.x, at.y, 7.5f, 7.0f, kSkin);
    c.ball(at.x, at.y + 4.0f, 6.5f, 2.6f, kHair);
    c.ball(at.x - 3.0f, at.y + 1.5f, 1.8f, 1.6f, kBile, 2);
    c.ball(at.x + 3.0f, at.y + 1.5f, 1.8f, 1.6f, kBile, 2);
    c.patch(at.x - 4.5f, at.y - 1.5f, 9.0f, 5.0f, kPMouth);
    for (int i = 0; i < 3; ++i) {
        c.patch(at.x - 3.5f + static_cast<float>(i) * 3.0f, at.y - 1.5f, 1.0f, 2.0f, kPTeeth);
        c.patch(at.x - 2.5f + static_cast<float>(i) * 3.0f, at.y - 5.0f, 1.0f, 2.0f, kPTeeth);
    }
}

void paintBossArmHuge(Canvas& c, const BossLookArt& look, Vec2 shoulder, Vec2 elbow, Vec2 hand, float r, int shift) {
    c.limb(shoulder.x, shoulder.y, elbow.x, elbow.y, r * 1.2f, r, kSkin, shift);
    c.limb(elbow.x, elbow.y, hand.x, hand.y, r, r * 0.8f, kSkin, shift);
    c.ball(hand.x, hand.y - 1.0f, r * 1.05f, r, kSkin, shift);
    // Claws. Hell's are longer and thicker.
    const float clawReach = look.horror >= 3 ? 2.6f : 1.8f;
    const float clawWidth = look.horror >= 3 ? 2.1f : 1.6f;
    for (int i = -1; i <= 1; ++i) {
        const float x = hand.x + static_cast<float>(i) * r * 0.7f;
        c.limb(x, hand.y - r * 0.6f, x + static_cast<float>(i), hand.y - r * clawReach, clawWidth, 0.5f, kBone, shift);
    }
    const float out = elbow.x < shoulder.x ? -1.0f : 1.0f;  // Away from the body.
    if (look.horror >= 1) {
        // The forearm has split open, and a broken bone pokes far out through the skin, so
        // it breaks the arm's outline as well as its color.
        const Vec2 along = hand - elbow;
        const float length = std::max(1.0f, std::sqrt(along.x * along.x + along.y * along.y));
        Vec2 side{-along.y / length, along.x / length};
        if (side.x * out < 0.0f) side = side * -1.0f;
        const Vec2 woundFrom = lerp(elbow, hand, 0.15f) + side * (r * 0.3f);
        const Vec2 woundTo = lerp(elbow, hand, 0.62f) + side * (r * 0.5f);
        c.limb(woundFrom.x, woundFrom.y, woundTo.x, woundTo.y, r * 0.5f, r * 0.36f, kBlood, shift);
        const Vec2 boneFrom = lerp(elbow, hand, 0.22f) + side * (r * 0.1f);
        const Vec2 boneTo = lerp(elbow, hand, 0.55f) + side * (r * 1.7f);
        c.limb(boneFrom.x, boneFrom.y, boneTo.x, boneTo.y, r * 0.3f, r * 0.26f, kBone, shift);
        c.ball(boneTo.x, boneTo.y, r * 0.4f, r * 0.34f, kBone, shift);
    }
    if (look.horror >= 2) {
        // A glowing split down the upper arm.
        const Vec2 a = lerp(shoulder, elbow, 0.2f) + Vec2{out * r * 0.4f, 0.0f};
        const Vec2 b = lerp(shoulder, elbow, 0.5f) + Vec2{out * r * 0.1f, 0.0f};
        const Vec2 d = lerp(shoulder, elbow, 0.85f) + Vec2{out * r * 0.5f, 0.0f};
        paintCrack(c, a, b, d);
    }
    if (look.horror >= 1 && look.phase == 2) {
        // Bone spurs out of the elbow.
        c.limb(elbow.x, elbow.y, elbow.x + out * 8.0f, elbow.y + 10.0f, 2.4f, 0.5f, kBone, shift);
        c.limb(elbow.x, elbow.y - 4.0f, elbow.x + out * 9.0f, elbow.y + 0.5f, 2.0f, 0.5f, kBone, shift);
    }
}

void paintBossDetails(Canvas& c, const BossLookArt& look, Vec2 chest) {
    if (look.phase == 0) {
        // Exposed ribs and torn overalls.
        for (int i = 0; i < 4; ++i) {
            const float rowY = chest.y + 7.0f - static_cast<float>(i) * 4.0f;
            c.limb(chest.x - 9.0f, rowY + 1.0f, chest.x + 9.0f, rowY + 1.0f, 0.9f, 0.9f, kBone);
        }
        if (look.horror == 0) {
            c.ball(chest.x, chest.y - 16.0f, 7.0f, 4.0f, kBlood);
            c.ball(chest.x - 1.0f, chest.y - 15.5f, 3.5f, 1.8f, Ramp{kPMouth, 1});
        }
        c.box(chest.x - 12.0f, chest.y + 2.0f, chest.x - 9.0f, chest.y + 15.0f, kPants);
        c.box(chest.x + 9.0f, chest.y + 2.0f, chest.x + 12.0f, chest.y + 15.0f, kPants);
    } else if (look.phase == 1) {
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
        if (look.horror >= 1) {
            // Rotten flesh bulges out between the plates, and a bar runs right through.
            c.setContour(true);
            paintGrowths(c, chest.x - 1.0f, chest.y + 8.0f, 4.0f, 3, 1.0f);
            c.limb(chest.x - 27.0f, chest.y - 13.0f, chest.x + 25.0f, chest.y + 4.0f, 1.8f, 1.5f, kMetal);
            c.setContour(false);
        }
    } else {
        // Flayed: glowing heart and burning veins.
        c.ball(chest.x, chest.y + 3.0f, 8.0f, 8.0f, kBile);
        c.ball(chest.x - 1.0f, chest.y + 4.0f, 4.5f, 4.5f, kBile, 2);
        c.limb(chest.x, chest.y - 4.0f, chest.x - 6.0f, chest.y - 19.0f, 1.2f, 0.7f, kBile, 1);
        c.limb(chest.x + 2.0f, chest.y - 4.0f, chest.x + 7.0f, chest.y - 18.0f, 1.2f, 0.7f, kBile, 1);
        c.limb(chest.x - 7.0f, chest.y + 2.0f, chest.x - 16.0f, chest.y + 9.0f, 1.0f, 0.6f, kBile, 1);
        c.limb(chest.x + 7.0f, chest.y + 2.0f, chest.x + 16.0f, chest.y + 9.0f, 1.0f, 0.6f, kBile, 1);
        if (look.horror >= 1) {
            // Bare ribs curl round the heart like a cage.
            for (int side = -1; side <= 1; side += 2) {
                const float s = static_cast<float>(side);
                for (int i = 0; i < 3; ++i) {
                    const float rowY = chest.y + 10.0f - static_cast<float>(i) * 5.5f;
                    c.limb(chest.x + s * 9.0f, rowY, chest.x + s * 19.0f, rowY - 2.5f, 1.4f, 1.1f, kBone);
                }
            }
        }
    }
}

// The second mouth, from Normal up: a gash across the belly that grows into a chest
// split from top to bottom in Hell, ringed with eyes.
void paintBossMaw(Canvas& c, const BossLookArt& look, Vec2 chest, bool front) {
    if (look.horror < 1) return;
    const float x = chest.x + (front ? 0.0f : 7.0f);
    if (look.horror >= 3) {
        paintMaw(c, x, chest.y - 4.0f, front ? 20.0f : 12.0f, 30.0f, true);
        for (int i = 0; i < 6; ++i) {
            const float side = i % 2 == 0 ? -1.0f : 1.0f;
            const float rowY = chest.y + 9.0f - static_cast<float>(i / 2) * 12.0f;
            paintSmallEye(c, x + side * (front ? 16.0f : 10.0f), rowY);
        }
        return;
    }
    const float w = look.horror >= 2 ? 22.0f : 17.0f;
    const float h = look.horror >= 2 ? 11.0f : 8.0f;
    paintMaw(c, x, chest.y - 15.0f, front ? w : w * 0.7f, h, false);
    if (look.horror >= 2) {
        paintSmallEye(c, x - w * 0.5f - 2.0f, chest.y - 8.0f);
        if (front) paintSmallEye(c, x + w * 0.5f + 2.0f, chest.y - 8.0f);
    }
    if (look.phase == 0) {
        // A loop of gut hangs out of it.
        c.limb(x + 3.0f, chest.y - 19.0f, x + 6.0f, chest.y - 29.0f, 3.0f, 2.5f, kGrowth);
        c.ball(x + 5.0f, chest.y - 30.0f, 3.6f, 3.0f, kGrowth);
    }
}

// From Normal up: a torn hole in the flank, with the ribs showing through.
void paintFlankRibs(Canvas& c, const BossLookArt& look, float x, float chestY) {
    if (look.horror < 1 || look.phase == 2) return;
    c.ball(x, chestY - 3.0f, 6.5f, 9.5f, kBlood);
    for (int i = 0; i < 3; ++i) {
        const float rowY = chestY + 3.0f - static_cast<float>(i) * 5.0f;
        c.limb(x - 5.0f, rowY, x + 5.0f, rowY - 1.5f, 1.5f, 1.2f, kBone);
    }
}

void paintBossSpikes(Canvas& c, const BossLookArt& look, float shoulderY, float headX, float headY) {
    if (look.phase == 1) {
        // Rebar through the shoulders.
        c.limb(-20.0f, shoulderY + 2.0f, -29.0f, shoulderY + 20.0f, 1.3f, 1.0f, kMetal);
        c.limb(-14.0f, shoulderY + 4.0f, -18.0f, shoulderY + 26.0f, 1.3f, 1.0f, kMetal);
        c.limb(20.0f, shoulderY + 2.0f, 27.0f, shoulderY + 22.0f, 1.3f, 1.0f, kMetal);
        c.limb(13.0f, shoulderY + 4.0f, 18.0f, shoulderY + 26.0f, 1.3f, 1.0f, kMetal);
    } else if (look.phase == 2) {
        // Bone spikes bursting out of the back and head. Normal and up grow more of them.
        const int reach = look.horror >= 1 ? 4 : 3;
        const float spacing = look.horror >= 1 ? 6.5f : 8.0f;
        for (int i = -reach; i <= reach; ++i) {
            const float x = static_cast<float>(i) * spacing;
            const float tall = 14.0f + 5.0f * (static_cast<float>(reach) - std::abs(static_cast<float>(i)));
            c.limb(x, shoulderY + 3.0f, x * 1.25f, shoulderY + tall, 2.4f, 0.5f, kBone);
        }
        c.limb(headX - 6.0f, headY + 8.0f, headX - 12.0f, headY + 20.0f, 2.0f, 0.5f, kBone);
        c.limb(headX + 6.0f, headY + 8.0f, headX + 12.0f, headY + 20.0f, 2.0f, 0.5f, kBone);
    }
}

// Tendrils from the back: two from Hard, four in Hell. They sway with the walk.
void paintBossTendrils(Canvas& c, const BossLookArt& look, Vec2 root, float spread, float t, bool sideView) {
    if (look.horror < 2) return;
    const float sway = 0.18f * std::sin(t * kTau);
    const float r = look.horror >= 3 ? 4.2f : 3.4f;
    const float length = look.horror >= 3 ? 40.0f : 32.0f;
    if (sideView) {
        // Seen from the side, they stream back and up from the hump.
        paintTendril(c, root, 2.2f + sway, length, -0.22f, r);
        if (look.horror >= 3) {
            paintTendril(c, root + Vec2{-4.0f, -8.0f}, 2.8f - sway, length * 0.9f, -0.18f, r * 0.9f);
            paintTendril(c, root + Vec2{4.0f, 4.0f}, 1.8f - sway, length * 0.8f, 0.2f, r * 0.85f);
        }
        return;
    }
    paintTendril(c, root + Vec2{-spread, 0.0f}, 2.0f + sway, length, 0.22f, r);
    paintTendril(c, root + Vec2{spread, 0.0f}, 1.14f - sway, length, -0.22f, r);
    if (look.horror >= 3) {
        paintTendril(c, root + Vec2{-spread * 1.6f, -6.0f}, 2.65f - sway, length * 0.95f, 0.26f, r * 0.9f);
        paintTendril(c, root + Vec2{spread * 1.6f, -6.0f}, 0.49f + sway, length * 0.95f, -0.26f, r * 0.9f);
    }
}

// Hell: a crown of bone spikes through the top of the skull.
void paintSpikeCrown(Canvas& c, const BossLookArt& look, Vec2 head) {
    if (look.horror < 3) return;
    for (int i = -2; i <= 2; ++i) {
        const float f = static_cast<float>(i);
        c.limb(head.x + f * 4.5f, head.y + 6.0f, head.x + f * 8.0f, head.y + 20.0f - std::abs(f) * 2.5f, 2.2f, 0.4f, kBone);
    }
}

void paintBossHeadFront(Canvas& c, const BossLookArt& look, Vec2 head) {
    const int h = look.horror;
    c.ball(head.x, head.y, 12.0f, 10.5f, kSkin);
    c.ball(head.x, head.y + 6.5f, 10.5f, 4.0f, kHair);
    if (h >= 2) {
        // Half the face has rotted away to the skull.
        c.ball(head.x + 5.0f, head.y + 0.5f, 7.5f, 9.0f, kBone);
        c.ball(head.x + 5.0f, head.y + 2.0f, 3.8f, 3.4f, Ramp{kPSocket, 1});
    }
    c.ball(head.x - 5.0f, head.y + 2.0f, 2.8f, 2.4f, kBile, 2);
    c.ball(head.x + 5.0f, head.y + 2.0f, 2.8f, 2.4f, kBile, 2);
    // From Normal up the jaw hangs lower, with longer teeth.
    const float drop = h >= 1 ? 3.0f : 0.0f;
    const float tooth = h >= 1 ? 3.0f : 2.0f;
    c.ball(head.x, head.y - 7.5f - drop, 9.0f + drop * 0.4f, 4.0f + drop * 0.3f, kSkin, -1);
    c.patch(head.x - 7.0f, head.y - 2.5f, 14.0f, 6.0f + drop, kPMouth);
    for (int i = 0; i < 6; ++i) {
        c.patch(head.x - 6.5f + static_cast<float>(i) * 2.5f, head.y - 2.5f, 1.0f, tooth, kPTeeth);
        c.patch(head.x - 5.5f + static_cast<float>(i) * 2.2f, head.y - 6.0f - drop, 1.0f, tooth, kPTeeth);
    }
    if (h >= 1) {
        paintSmallEye(c, head.x - 6.5f, head.y + 7.5f);
        paintSmallEye(c, head.x + 9.5f, head.y + 5.5f);
        c.limb(head.x - 4.0f, head.y - 12.0f, head.x - 4.5f, head.y - 21.0f, 1.3f, 0.6f, kBlood);
        c.limb(head.x + 3.0f, head.y - 12.5f, head.x + 3.5f, head.y - 18.0f, 1.1f, 0.6f, kBlood);
    }
    if (h >= 2) {
        // Tusks out of the corners of the jaw.
        c.limb(head.x - 7.5f, head.y - 9.0f - drop, head.x - 13.0f, head.y - 1.0f, 2.4f, 0.6f, kBone);
        c.limb(head.x + 7.5f, head.y - 9.0f - drop, head.x + 13.0f, head.y - 1.0f, 2.4f, 0.6f, kBone);
    }
    c.setContour(true);
    if (h >= 1) paintGrowths(c, head.x - 13.0f, head.y + 8.0f, 3.5f + 1.5f * static_cast<float>(h), 3, -1.0f);
    paintSpikeCrown(c, look, head);
    c.setContour(false);
}

void paintBossHeadSide(Canvas& c, const BossLookArt& look, Vec2 head) {
    const int h = look.horror;
    c.ball(head.x, head.y, 11.0f, 10.5f, kSkin);
    c.ball(head.x - 3.0f, head.y + 5.0f, 8.5f, 6.0f, kHair);
    if (h >= 2) {
        c.ball(head.x + 5.0f, head.y + 1.5f, 6.5f, 8.5f, kBone);
        c.ball(head.x + 5.0f, head.y + 2.5f, 3.6f, 3.2f, Ramp{kPSocket, 1});
    }
    // Burning eye, and a jaw that could take a head off.
    c.ball(head.x + 5.0f, head.y + 2.5f, 2.8f, 2.4f, kBile, 2);
    const float drop = h >= 1 ? 3.0f : 0.0f;
    const float tooth = h >= 1 ? 3.0f : 2.0f;
    c.ball(head.x + 3.0f, head.y - 7.5f - drop, 8.0f + drop * 0.4f, 4.0f + drop * 0.3f, kSkin, -1);
    c.patch(head.x - 1.0f, head.y - 2.0f, 11.0f, 5.0f + drop, kPMouth);
    for (int i = 0; i < 4; ++i) {
        c.patch(head.x + static_cast<float>(i) * 2.8f, head.y - 2.0f, 1.0f, tooth, kPTeeth);
        c.patch(head.x + 1.0f + static_cast<float>(i) * 2.6f, head.y - 5.0f - drop, 1.0f, tooth, kPTeeth);
    }
    if (h >= 1) {
        paintSmallEye(c, head.x + 0.5f, head.y + 7.5f);
        c.limb(head.x + 7.0f, head.y - 12.0f, head.x + 7.5f, head.y - 21.0f, 1.3f, 0.6f, kBlood);
    }
    if (h >= 2) c.limb(head.x + 9.0f, head.y - 9.0f - drop, head.x + 15.0f, head.y - 1.0f, 2.4f, 0.6f, kBone);
    c.setContour(true);
    if (h >= 1) paintGrowths(c, head.x - 9.0f, head.y + 3.0f, 3.5f + 1.5f * static_cast<float>(h), 3, 1.0f);
    paintSpikeCrown(c, look, Vec2{head.x - 2.0f, head.y});
    c.setContour(false);
}

void paintBoss(Canvas& c, const BossLookArt& look, int facing, int pose, float t) {
    const bool charge = pose == kPoseBraceA || pose == kPoseBraceB;
    const bool dash = pose == kPoseDashA;
    const float angle = t * kTau;
    const float swing = 6.0f * std::sin(angle);
    float bob = 2.0f * std::abs(std::sin(angle)) - 1.0f;
    if (charge) bob = pose == kPoseBraceA ? -5.0f : -6.5f;
    if (dash) bob = -3.0f;
    const float raise = charge ? (pose == kPoseBraceA ? 22.0f : 26.0f) : 0.0f;
    const bool metalShoulders = look.phase == 1;
    const float h = static_cast<float>(look.horror);
    // From Hard up one arm has swollen, so the two sides no longer match.
    const float swollen = look.horror >= 2 ? 1.28f : 1.0f;

    if (facing == kFacingSide) {
        const float lean = dash ? 14.0f : 0.0f;
        const Vec2 hip{-3.0f, 36.0f + bob};
        const Vec2 chest{2.0f + lean, 62.0f + bob};
        const Vec2 head{10.0f + lean * 1.6f, 87.0f + bob - (dash ? 8.0f : 0.0f)};
        const Vec2 nearFoot = footPath(t, -4.0f, 10.0f, 7.0f);
        const Vec2 farFoot = footPath(t + 0.5f, 2.0f, 10.0f, 7.0f);
        const Vec2 nearShoulder{chest.x - 15.0f, chest.y + 12.0f};
        Body legs;
        legs.legTop = 10.5f;
        legs.legBottom = 7.5f;
        legs.shoeLength = 6.0f;
        legs.kneeBend = 2.0f;
        paintBossTendrils(c, look, Vec2{nearShoulder.x + 2.0f, nearShoulder.y + 8.0f}, 0.0f, t, true);
        paintBossArmHuge(c, look, Vec2{chest.x + 8.0f, chest.y + 12.0f}, Vec2{chest.x + 20.0f, chest.y - 8.0f + raise}, Vec2{chest.x + 22.0f + swing * 0.5f, chest.y - 32.0f + raise * 2.0f}, 8.0f, -1);
        paintLeg(c, legs, Vec2{hip.x + 4.0f, hip.y}, farFoot, -1);
        paintLeg(c, legs, Vec2{hip.x - 4.0f, hip.y}, nearFoot, 0);
        c.ball(hip.x, hip.y + 2.0f, 18.0f, 13.0f, kPants);
        c.limb(hip.x, hip.y + 10.0f, chest.x, chest.y, 18.0f, 24.0f, kShirt);
        c.ball(chest.x + 2.0f, chest.y + 10.0f, 25.0f, 15.0f, kShirt);
        paintBossDetails(c, look, Vec2{chest.x + 3.0f, chest.y + 0.0f});
        paintBossMaw(c, look, Vec2{chest.x + 3.0f, chest.y}, false);
        paintFlankRibs(c, look, chest.x - 6.0f, chest.y + 2.0f);
        if (look.horror >= 2) paintCrack(c, Vec2{chest.x + 14.0f, chest.y + 14.0f}, Vec2{chest.x + 8.0f, chest.y + 6.0f}, Vec2{chest.x + 13.0f, chest.y - 4.0f});
        paintBossSpikes(c, look, chest.y + 16.0f, head.x, head.y);
        paintBossHeadSide(c, look, head);
        c.setContour(true);
        // The near arm hangs at the back of the body, leaving the chest's detail in view.
        c.ball(nearShoulder.x, nearShoulder.y, 11.0f, 10.0f, metalShoulders ? kMetal : kShirt);
        if (look.horror >= 1) paintGrowths(c, nearShoulder.x - 2.0f, nearShoulder.y + 10.0f, 5.5f + 1.5f * h, 5, 1.0f);
        if (look.horror >= 3) paintSecondHead(c, Vec2{nearShoulder.x + 6.0f, nearShoulder.y + 20.0f});
        paintBossArmHuge(c, look, nearShoulder, Vec2{nearShoulder.x - 5.0f - swing * 0.5f, chest.y - 14.0f + raise}, Vec2{nearShoulder.x - 1.0f - swing, chest.y - 38.0f + raise * 2.0f}, 7.5f * swollen, 0);
        c.setContour(false);
    } else {
        const bool front = facing == kFacingFront;
        const float crouch = dash ? 10.0f : 0.0f;
        const Vec2 chest{0.0f, 62.0f + bob - crouch};
        const Vec2 head{0.0f, (front ? 88.0f : 90.0f) + bob - crouch * 1.5f};
        // Facing the camera the tendrils grow behind the body, so they go first.
        if (front) paintBossTendrils(c, look, Vec2{0.0f, chest.y + 20.0f}, 14.0f, t, false);
        Body legs;
        legs.legTop = 10.5f;
        legs.legBottom = 7.5f;
        paintFrontLeg(c, legs, Vec2{-13.0f, 34.0f + bob}, footPathFacing(t + 0.5f, -14.0f, 7.0f, 7.0f), 0);
        paintFrontLeg(c, legs, Vec2{13.0f, 34.0f + bob}, footPathFacing(t, 14.0f, 7.0f, 7.0f), 0);
        c.ball(0.0f, 37.0f + bob, 22.0f, 13.0f, kPants);
        c.limb(0.0f, 44.0f + bob, 0.0f, chest.y, 22.0f, 27.0f, kShirt);
        c.ball(0.0f, chest.y + 11.0f, 33.0f, 14.0f, kShirt);
        if (front) {
            // In Hell the phase-three heart beats inside the split chest, so the mouth goes under it.
            const bool heartInMaw = look.horror >= 3 && look.phase == 2;
            if (heartInMaw) paintBossMaw(c, look, chest, true);
            paintBossDetails(c, look, chest);
            if (!heartInMaw) paintBossMaw(c, look, chest, true);
            paintFlankRibs(c, look, 20.0f, chest.y);
            if (look.horror >= 2) {
                paintCrack(c, Vec2{-22.0f, chest.y + 8.0f}, Vec2{-15.0f, chest.y}, Vec2{-19.0f, chest.y - 10.0f});
                paintCrack(c, Vec2{9.0f, chest.y - 22.0f}, Vec2{15.0f, chest.y - 27.0f}, Vec2{11.0f, chest.y - 33.0f});
            }
        } else {
            c.patch(-11.0f, chest.y + 12.0f, 5.0f, 30.0f, kPSkin + 1);
            c.patch(5.0f, chest.y + 2.0f, 4.0f, 18.0f, kPSkin + 1);
            if (look.horror >= 1) {
                // The spine has burst out through the skin of the back.
                c.setContour(true);
                for (int i = 0; i < 6; ++i) c.ball(0.0f, chest.y + 15.0f - static_cast<float>(i) * 6.0f, 3.6f, 2.5f, kBone);
                paintGrowths(c, 13.0f, chest.y + 2.0f, 5.0f + 1.5f * h, 5, 1.0f);
                if (look.horror >= 2) paintCrack(c, Vec2{-16.0f, chest.y + 10.0f}, Vec2{-9.0f, chest.y + 1.0f}, Vec2{-14.0f, chest.y - 10.0f});
                c.setContour(false);
            }
            c.setContour(true);
            paintBossTendrils(c, look, Vec2{0.0f, chest.y + 14.0f}, 10.0f, t, false);
            c.setContour(false);
        }
        paintBossSpikes(c, look, chest.y + 16.0f, head.x, head.y);
        if (front) {
            paintBossHeadFront(c, look, head);
        } else {
            c.ball(head.x, head.y, 12.0f, 10.5f, kSkin);
            c.ball(head.x, head.y + 1.5f, 11.0f, 8.5f, kHair);
            c.setContour(true);
            paintSpikeCrown(c, look, head);
            c.setContour(false);
        }
        c.setContour(true);
        const float armOut = 36.0f;
        // Hell's left arm hangs lower than its right.
        const float longArm = look.horror >= 3 ? 8.0f : 0.0f;
        const float bulge = (swollen - 1.0f) * 8.0f;
        paintBossArmHuge(c, look, Vec2{-armOut + 6.0f, chest.y + 11.0f}, Vec2{-armOut - 5.0f, chest.y - 12.0f + raise * 0.8f + swing * 0.4f},
                         Vec2{-armOut - 2.0f, chest.y - 36.0f - longArm + raise * 2.2f + swing}, 9.5f, 0);
        paintBossArmHuge(c, look, Vec2{armOut - 6.0f, chest.y + 11.0f}, Vec2{armOut + 5.0f + bulge, chest.y - 12.0f + raise * 0.8f - swing * 0.4f},
                         Vec2{armOut + 2.0f + bulge, chest.y - 36.0f + raise * 2.2f - swing}, 9.5f * swollen, 0);
        c.ball(-armOut + 7.0f, chest.y + 11.0f, 13.0f, 11.0f, metalShoulders ? kMetal : kShirt);
        c.ball(armOut - 7.0f, chest.y + 11.0f, 13.0f * swollen, 11.0f * swollen, metalShoulders ? kMetal : kShirt);
        if (look.horror >= 1) {
            // A big cluster of boils bursts out of one shoulder and breaks the outline.
            // Lopsided on purpose: it reads as sick.
            paintGrowths(c, -armOut + 2.0f, chest.y + 23.0f, 6.5f + 1.5f * h, 5, 1.0f);
            if (look.horror == 2) paintGrowths(c, armOut - 3.0f, chest.y + 22.0f, 7.0f, 4, -1.0f);
            if (look.horror >= 3) paintSecondHead(c, Vec2{armOut - 8.0f, chest.y + 26.0f});
        }
        c.setContour(false);
    }
}

void paintFigureBody(Canvas& c, Figure figure, Difficulty bossLevel, int facing, int pose, float t) {
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
        case Figure::Boss1:
        case Figure::Boss2:
        case Figure::Boss3: {
            const BossLookArt look{static_cast<int>(figure) - static_cast<int>(Figure::Boss1), static_cast<int>(bossLevel)};
            paintBoss(c, look, facing, pose, t);
            break;
        }
        case Figure::Count: break;
    }
}

}  // namespace

float bossArtScale(Difficulty level) { return kBossArtScale[static_cast<int>(level)]; }

void paintFigure(Canvas& c, Figure figure, Difficulty bossLevel, int facing, int pose) {
    const bool boss = figure >= Figure::Boss1;
    const float t = static_cast<float>(pose) / static_cast<float>(boss ? kBossWalkFrames : kWalkFrames);
    paintFigureBody(c, figure, bossLevel, facing, pose, t);
    c.outline(kPOutline);
}

void paintCorpse(Canvas& c, Figure figure, Difficulty bossLevel, int frame) {
    // A falling corpse is a walking pose, turned around its feet. The last frame
    // adds a pool of blood underneath, so the pile of dead reads as a battle scar.
    static constexpr float kTurns[kCorpseFrames] = {0.45f, 1.05f, 1.48f};
    const bool boss = figure >= Figure::Boss1;
    const float height = boss ? 40.0f : (figure == Figure::Brute || figure == Figure::Bloater ? 14.0f : 8.0f);
    // The boss is too long to lie down around its feet inside the canvas, so it is painted
    // a little up and to the right of where it fell, which keeps all of it in the picture.
    const float shiftX = boss ? 40.0f : 0.0f;
    const float shiftY = boss ? 24.0f : 0.0f;
    if (frame == kCorpseFrames - 1) {
        c.setFrame(shiftX, shiftY, 1.0f, 0.0f);
        c.ball(-height * 1.1f, 2.0f, height * 1.4f, height * 0.45f, kBlood);
    }
    const float turn = figure == Figure::Crawler ? kTurns[frame] * 0.15f : kTurns[frame];
    c.setFrame(shiftX, shiftY, std::cos(turn), std::sin(turn));
    paintFigureBody(c, figure, bossLevel, kFacingSide, 0, 0.25f);
    c.clearFrame();
    c.outline(kPOutline);
}

}  // namespace ls::worldview
