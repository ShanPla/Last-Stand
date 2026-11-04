#include "components/WorldView/Figures.h"

#include "data/Balance.h"

#include <algorithm>
#include <cmath>

namespace ls::worldview {

namespace {

constexpr int kMaxBakedQuads = 16000;
constexpr int kFull = 0;
constexpr int kCheap = 1;

// Outline width in world units, per figure, turned into figure units by dividing
// by the radius. Small zombies get thinner lines so their fill still shows.
constexpr float kWalkerLine = 2.2f, kRunnerLine = 1.7f, kBruteLine = 3.0f, kSpitterLine = 2.0f;
constexpr float kBloaterLine = 2.4f, kCrawlerLine = 1.7f, kChargerLine = 2.5f, kBossLine = 4.5f;

float radiusOf(EnemyType type) { return balance::kEnemyStats[static_cast<int>(type)].radius; }

// The foot moves forward while lifted, and backward while planted on the ground.
Vec2 footPath(float hipX, float phase, float stride, float lift) {
    return {hipX - std::cos(phase) * stride, std::max(0.0f, std::sin(phase)) * lift};
}

void bentLimb(Rig& rig, Vec2 from, Vec2 joint, Vec2 to, float widthFrom, float widthJoint, float widthTo) {
    rig.limb(from, joint, widthFrom, widthJoint);
    rig.limb(joint, to, widthJoint, widthTo);
}

// A leg with a knee that always bends forward, however far apart hip and foot are.
void bentLeg(Rig& rig, Vec2 hip, Vec2 foot, float kneeForward, float widthHip, float widthKnee, float widthFoot) {
    const Vec2 knee = Vec2{(hip.x + foot.x) * 0.5f + kneeForward, (hip.y + foot.y) * 0.5f};
    bentLimb(rig, hip, knee, foot, widthHip, widthKnee, widthFoot);
}

void poseWalker(Rig& rig, float t, bool detailed) {
    const float phi = t * kTau;
    // It limps: the body drops each time the short leg takes the weight.
    const float dip = 0.11f * (0.5f + 0.5f * std::sin(phi));
    const float sway = 0.07f * std::sin(phi);
    const Vec2 hip{-0.05f + sway * 0.5f, 1.28f - dip};
    const Vec2 chest{0.38f + sway, 2.12f - dip};
    const Vec2 head{0.86f + sway * 1.6f, 2.52f - dip - 0.05f * std::sin(phi + 1.0f)};
    const Vec2 nearFoot = footPath(0.05f, phi, 0.62f, 0.32f);
    const Vec2 farFoot = footPath(-0.08f, phi + kPi, 0.30f, 0.10f);
    const Vec2 hand{1.62f, 2.12f + sway * 1.4f};

    if (!detailed) {
        rig.limb(hip, farFoot, 0.24f, 0.12f);
        rig.limb(hip, nearFoot, 0.26f, 0.14f);
        rig.limb(hip, chest, 0.42f, 0.56f);
        rig.limb(chest, hand, 0.20f, 0.05f);
        rig.blob(head, 0.44f, 0.44f, 4);
        rig.endGroup();
        return;
    }
    bentLeg(rig, hip, farFoot, 0.20f, 0.22f, 0.15f, 0.09f);
    bentLimb(rig, chest, Vec2{0.72f, 1.60f + sway}, Vec2{1.06f, 1.32f + sway}, 0.17f, 0.12f, 0.02f);
    rig.endGroup();

    bentLeg(rig, hip, nearFoot, 0.26f, 0.26f, 0.17f, 0.11f);
    rig.limb(hip, chest, 0.44f, 0.60f);
    rig.blob(head, 0.46f, 0.42f, 6, 0.3f);
    rig.spike(head + Vec2{0.10f, -0.22f}, head + Vec2{0.56f, -0.46f}, 0.16f);
    rig.detailBlob(head + Vec2{0.18f, 0.06f}, 0.12f, 0.10f, 4);
    rig.endGroup();

    bentLimb(rig, chest + Vec2{0.02f, 0.02f}, Vec2{1.02f, 2.00f + sway}, hand, 0.19f, 0.14f, 0.02f);
    rig.endGroup();
}

void poseRunner(Rig& rig, float t, bool detailed) {
    const float phi = t * kTau;
    const float bob = 0.08f * std::abs(std::sin(phi));
    // Hips far behind the head: a sprinter falling forward.
    const Vec2 hip{-0.35f, 1.20f + bob};
    const Vec2 chest{0.55f, 1.72f + bob};
    const Vec2 head{1.22f, 1.86f + bob};
    const Vec2 nearFoot = footPath(-0.30f, phi, 0.95f, 0.70f);
    const Vec2 farFoot = footPath(-0.30f, phi + kPi, 0.95f, 0.70f);
    const float pump = 0.32f * std::sin(phi);

    if (!detailed) {
        rig.limb(hip, farFoot, 0.22f, 0.08f);
        rig.limb(hip, nearFoot, 0.22f, 0.08f);
        rig.limb(hip, chest, 0.32f, 0.40f);
        rig.limb(chest, Vec2{1.75f + pump, 1.40f}, 0.16f, 0.03f);
        rig.blob(head, 0.42f, 0.38f, 4);
        rig.endGroup();
        return;
    }
    bentLeg(rig, hip, farFoot, 0.36f, 0.20f, 0.13f, 0.06f);
    bentLimb(rig, chest, Vec2{0.30f - pump, 1.16f + bob}, Vec2{-0.30f - pump, 1.46f + bob}, 0.15f, 0.11f, 0.02f);
    rig.endGroup();

    bentLeg(rig, hip, nearFoot, 0.36f, 0.22f, 0.14f, 0.06f);
    rig.limb(hip, chest, 0.32f, 0.42f);
    rig.blob(head, 0.42f, 0.36f, 6, 0.2f);
    rig.spike(head + Vec2{0.18f, -0.12f}, head + Vec2{0.72f, -0.26f}, 0.13f);
    rig.detailBlob(head + Vec2{0.17f, 0.05f}, 0.13f, 0.11f, 4);
    bentLimb(rig, chest, Vec2{1.05f + pump, 1.25f + bob}, Vec2{1.75f + pump, 1.42f + bob}, 0.15f, 0.11f, 0.02f);
    rig.endGroup();
}

void poseBrute(Rig& rig, float t, bool detailed) {
    const float phi = t * kTau;
    // A heavy stomp: a slow rise, then the weight drops hard as a foot lands.
    const float wave = std::abs(std::sin(phi));
    const float lift = 1.0f - (1.0f - wave) * (1.0f - wave) * (1.0f - wave);
    const float y = 0.12f * lift - 0.05f;
    const float swing = 0.14f * std::sin(phi);
    const Vec2 nearFoot = footPath(0.30f, phi, 0.28f, 0.28f);
    const Vec2 farFoot = footPath(-0.30f, phi + kPi, 0.28f, 0.28f);
    const Vec2 nearHip{0.30f, 1.02f + y};
    const Vec2 farHip{-0.30f, 1.02f + y};
    const Vec2 head{0.30f, 2.62f + y};

    if (!detailed) {
        rig.limb(farHip, farFoot, 0.30f, 0.26f);
        rig.limb(nearHip, nearFoot, 0.30f, 0.26f);
        rig.limb(Vec2{1.20f, 2.10f + y}, Vec2{1.62f + swing, 0.90f + y}, 0.32f, 0.26f);
        rig.limb(Vec2{-1.00f, 2.10f + y}, Vec2{-1.38f - swing, 0.95f + y}, 0.30f, 0.24f);
        rig.endGroup();
        // A separate group, so a bright edge divides the arms from the wide chest.
        const Vec2 body[4] = {Vec2{-1.15f, 2.42f + y}, Vec2{1.35f, 2.42f + y}, Vec2{0.55f, 0.95f + y}, Vec2{-0.55f, 0.95f + y}};
        rig.hull(body, 4);
        rig.blob(head, 0.28f, 0.28f, 4);
        rig.spike(Vec2{-1.00f, 2.30f + y}, Vec2{-1.50f, 2.76f + y}, 0.20f);
        rig.spike(Vec2{1.20f, 2.30f + y}, Vec2{1.72f, 2.80f + y}, 0.20f);
        rig.endGroup();
        return;
    }
    bentLeg(rig, farHip, farFoot, 0.10f, 0.34f, 0.28f, 0.22f);
    bentLimb(rig, Vec2{-0.95f, 2.08f + y}, Vec2{-1.35f - swing, 1.42f + y}, Vec2{-1.30f - swing, 0.86f + y}, 0.32f, 0.26f, 0.22f);
    rig.blob(Vec2{-1.30f - swing, 0.78f + y}, 0.30f, 0.28f, 6);
    rig.endGroup();

    bentLeg(rig, nearHip, nearFoot, 0.12f, 0.36f, 0.30f, 0.24f);
    rig.spike(Vec2{0.10f, 2.46f + y}, head + Vec2{0.42f, 0.42f}, 0.14f);
    rig.blob(head, 0.28f, 0.26f, 6, 0.2f);
    const Vec2 body[8] = {Vec2{-1.20f, 1.80f + y}, Vec2{-1.05f, 2.25f + y}, Vec2{-0.35f, 2.48f + y}, Vec2{0.75f, 2.48f + y},
                          Vec2{1.40f, 2.25f + y},  Vec2{1.45f, 1.80f + y},  Vec2{0.60f, 0.98f + y},  Vec2{-0.55f, 0.98f + y}};
    rig.hull(body, 8);
    rig.spike(Vec2{-1.05f, 2.20f + y}, Vec2{-1.50f, 2.72f + y}, 0.18f);
    rig.spike(Vec2{1.30f, 2.22f + y}, Vec2{1.72f, 2.78f + y}, 0.18f);
    rig.detailBlob(head + Vec2{0.11f, 0.04f}, 0.08f, 0.07f, 4);
    rig.detailLimb(Vec2{-0.30f, 1.75f + y}, Vec2{0.75f, 1.75f + y}, 0.03f, 0.03f, kLayerOutline);
    rig.detailLimb(Vec2{-0.20f, 1.42f + y}, Vec2{0.60f, 1.42f + y}, 0.03f, 0.03f, kLayerOutline);
    rig.endGroup();

    bentLimb(rig, Vec2{1.18f, 2.08f + y}, Vec2{1.62f + swing, 1.42f + y}, Vec2{1.58f + swing, 0.86f + y}, 0.34f, 0.28f, 0.24f);
    rig.blob(Vec2{1.60f + swing, 0.76f + y}, 0.33f, 0.30f, 6);
    rig.endGroup();
}

void poseSpitter(Rig& rig, float t, bool detailed) {
    const float phi = t * kTau;
    const float bob = 0.05f * std::abs(std::sin(phi));
    const float swell = 0.05f * std::sin(phi * 2.0f);
    const Vec2 hip{0.0f, 2.02f + bob};
    const Vec2 chest{0.18f, 3.00f + bob};
    const Vec2 throat{0.45f, 3.38f + bob};
    const Vec2 head{0.86f, 3.98f + bob};
    const Vec2 nearFoot = footPath(0.02f, phi, 0.55f, 0.22f);
    const Vec2 farFoot = footPath(-0.02f, phi + kPi, 0.55f, 0.22f);

    if (!detailed) {
        rig.limb(hip, farFoot, 0.14f, 0.05f);
        rig.limb(hip, nearFoot, 0.14f, 0.05f);
        rig.limb(hip, chest, 0.18f, 0.22f);
        rig.blob(throat, 0.62f, 0.58f, 6);
        rig.blob(head, 0.26f, 0.24f, 4);
        rig.endGroup();
        return;
    }
    bentLeg(rig, hip, farFoot, 0.30f, 0.14f, 0.10f, 0.05f);
    bentLimb(rig, chest, Vec2{0.34f, 2.42f + bob}, Vec2{0.66f, 2.14f + bob}, 0.10f, 0.08f, 0.01f);
    rig.endGroup();

    bentLeg(rig, hip, nearFoot, 0.34f, 0.15f, 0.10f, 0.05f);
    rig.limb(hip, chest, 0.18f, 0.24f);
    rig.blob(head, 0.27f, 0.24f, 6);
    rig.spike(head + Vec2{0.12f, -0.08f}, head + Vec2{0.56f, -0.26f}, 0.12f);
    rig.blob(throat, 0.62f + swell, 0.58f + swell, 8);
    rig.detailBlob(throat + Vec2{0.08f, 0.0f}, 0.34f + swell, 0.32f + swell, 6);
    rig.detailBlob(head + Vec2{0.10f, 0.05f}, 0.08f, 0.07f, 4, kLayerHot);
    bentLimb(rig, chest + Vec2{0.02f, -0.06f}, Vec2{0.70f, 2.52f + bob}, Vec2{1.18f, 2.72f + bob}, 0.10f, 0.08f, 0.01f);
    rig.endGroup();
}

void poseBloater(Rig& rig, float t, bool detailed) {
    const float phi = t * kTau;
    // The belly lags behind the legs, then squashes as it settles.
    const float wobble = std::sin(phi);
    const float squash = 0.07f * std::sin(phi * 2.0f);
    const Vec2 belly{0.06f + wobble * 0.10f, 1.42f - squash * 0.5f};
    const float rx = 1.22f + squash;
    const float ry = 1.02f - squash;
    const Vec2 head{0.42f + wobble * 0.14f, 2.62f - squash};
    const Vec2 nearFoot = footPath(0.30f, phi, 0.26f, 0.20f);
    const Vec2 farFoot = footPath(-0.30f, phi + kPi, 0.26f, 0.20f);
    const Vec2 nearHip{0.32f, 0.62f};
    const Vec2 farHip{-0.32f, 0.62f};

    if (!detailed) {
        rig.limb(farHip, farFoot, 0.18f, 0.14f);
        rig.limb(nearHip, nearFoot, 0.18f, 0.14f);
        rig.blob(belly, rx, ry, 8, kPi / 8.0f);
        rig.blob(head, 0.28f, 0.26f, 4);
        rig.endGroup();
        return;
    }
    rig.limb(farHip, farFoot, 0.20f, 0.13f);
    rig.limb(Vec2{-0.72f + wobble * 0.08f, 1.92f}, Vec2{-1.18f, 1.62f + wobble * 0.08f}, 0.13f, 0.02f);
    rig.endGroup();

    rig.limb(nearHip, nearFoot, 0.21f, 0.14f);
    // A few spines keep the round belly reading as a threat, not a reward.
    rig.spike(belly + Vec2{-0.72f * rx, 0.62f * ry}, belly + Vec2{-1.12f * rx, 1.02f * ry}, 0.14f);
    rig.spike(belly + Vec2{-0.98f * rx, -0.05f * ry}, belly + Vec2{-1.42f * rx, -0.08f * ry}, 0.14f);
    rig.spike(belly + Vec2{-0.68f * rx, -0.68f * ry}, belly + Vec2{-1.05f * rx, -1.02f * ry}, 0.13f);
    rig.spike(belly + Vec2{0.05f * rx, 0.95f * ry}, belly + Vec2{-0.22f * rx, 1.42f * ry}, 0.14f);
    rig.blob(head, 0.30f, 0.27f, 6);
    rig.detailBlob(head + Vec2{0.12f, 0.04f}, 0.08f, 0.07f, 4);
    rig.blob(belly, rx, ry, 12, 0.13f);
    rig.detailBlob(belly + Vec2{0.30f, -0.04f}, 0.52f + squash, 0.46f - squash, 6);
    rig.detailBlob(belly + Vec2{0.30f, -0.04f}, 0.42f + squash, 0.36f - squash, 6, kLayerFill);
    rig.detailBlob(belly + Vec2{0.34f, -0.04f}, 0.14f, 0.12f, 4, kLayerHot);
    rig.limb(belly + Vec2{0.84f, 0.42f}, Vec2{1.68f + wobble * 0.06f, 1.76f}, 0.13f, 0.02f);
    rig.endGroup();
}

void poseCrawler(Rig& rig, float t, bool detailed) {
    const float phi = t * kTau;
    const float bob = 0.05f * std::sin(phi * 2.0f);
    const Vec2 rear{-1.05f, 0.72f + bob};
    const Vec2 front{0.62f, 0.86f + bob};
    const Vec2 head{1.30f, 0.74f + bob};
    const Vec2 backNear = footPath(-1.00f, phi, 0.42f, 0.24f);
    const Vec2 backFar = footPath(-0.62f, phi + kPi, 0.42f, 0.24f);
    const Vec2 frontNear = footPath(1.05f, phi + kPi, 0.42f, 0.24f);
    const Vec2 frontFar = footPath(0.65f, phi, 0.42f, 0.24f);

    if (!detailed) {
        rig.limb(Vec2{-0.70f, 0.75f + bob}, backNear, 0.15f, 0.04f);
        rig.limb(Vec2{0.55f, 0.85f + bob}, frontNear, 0.15f, 0.04f);
        rig.limb(rear, front, 0.26f, 0.38f);
        rig.blob(head, 0.42f, 0.36f, 4);
        rig.endGroup();
        return;
    }
    // Knees rise above the body, which gives it a spider's outline.
    const auto spiderLeg = [&](Vec2 hip, Vec2 foot) {
        const Vec2 knee{(hip.x + foot.x) * 0.5f, hip.y + 0.46f};
        bentLimb(rig, hip, knee, foot, 0.13f, 0.11f, 0.03f);
    };
    spiderLeg(Vec2{-0.62f, 0.78f + bob}, backFar);
    spiderLeg(Vec2{0.42f, 0.86f + bob}, frontFar);
    rig.endGroup();

    rig.limb(rear, front, 0.26f, 0.40f);
    rig.spike(Vec2{-0.60f, 0.92f + bob}, Vec2{-0.95f, 1.40f + bob}, 0.14f);
    rig.spike(Vec2{0.05f, 1.04f + bob}, Vec2{-0.20f, 1.56f + bob}, 0.15f);
    rig.blob(head, 0.44f, 0.36f, 6, 0.3f);
    rig.spike(head + Vec2{0.18f, -0.16f}, head + Vec2{0.68f, -0.36f}, 0.13f);
    rig.detailBlob(head + Vec2{0.18f, 0.06f}, 0.13f, 0.11f, 4);
    rig.endGroup();

    spiderLeg(Vec2{-0.78f, 0.72f + bob}, backNear);
    spiderLeg(Vec2{0.68f, 0.86f + bob}, frontNear);
    rig.endGroup();
}

struct ChargerStance {
    float lean;      // How far the upper body pushes forward. Negative rears back.
    float crouch;    // How much lower the body sits.
    float stride;
    float lift;
    float armDrop;   // Arms hang lower as this grows.
};

void poseCharger(Rig& rig, float t, bool detailed, const ChargerStance& s) {
    const float phi = t * kTau;
    const float bob = 0.06f * std::abs(std::sin(phi));
    const Vec2 hip{-0.05f - s.lean * 0.15f, 1.18f - s.crouch + bob};
    const Vec2 chest{0.25f + s.lean * 0.70f, 2.02f - s.crouch - s.lean * 0.22f + bob};
    const Vec2 head = chest + Vec2{0.68f, 0.02f - s.lean * 0.12f};
    const Vec2 nearFoot = footPath(0.10f - s.lean * 0.20f, phi, s.stride, s.lift);
    const Vec2 farFoot = footPath(-0.15f - s.lean * 0.20f, phi + kPi, s.stride, s.lift);
    const Vec2 hornBase = head + Vec2{0.10f, 0.14f};
    const Vec2 hornTip = head + Vec2{1.05f, 0.62f};
    const Vec2 lowerHornTip = head + Vec2{1.10f, -0.14f};

    if (!detailed) {
        rig.limb(hip, farFoot, 0.24f, 0.13f);
        rig.limb(hip, nearFoot, 0.26f, 0.14f);
        rig.limb(hip, chest, 0.44f, 0.68f);
        rig.blob(head, 0.34f, 0.34f, 4);
        rig.spike(hornBase, hornTip, 0.18f);
        rig.endGroup();
        return;
    }
    bentLeg(rig, hip, farFoot, 0.20f, 0.26f, 0.18f, 0.11f);
    bentLimb(rig, chest, chest + Vec2{0.42f, -0.46f - s.armDrop}, chest + Vec2{0.86f, -0.34f - s.armDrop}, 0.18f, 0.14f, 0.02f);
    rig.endGroup();

    bentLeg(rig, hip, nearFoot, 0.24f, 0.28f, 0.19f, 0.12f);
    rig.limb(hip, chest + Vec2{-0.05f, -0.10f}, 0.40f, 0.54f);
    rig.blob(head, 0.36f, 0.34f, 6);
    rig.spike(hornBase, hornTip, 0.18f);
    rig.spike(head + Vec2{0.12f, -0.10f}, lowerHornTip, 0.16f);
    // The armor plate covers the chest and points forward, like a battering ram.
    const Vec2 plate[6] = {chest + Vec2{-0.42f, 0.62f}, chest + Vec2{0.36f, 0.72f}, chest + Vec2{0.86f, 0.18f},
                           chest + Vec2{0.72f, -0.52f}, chest + Vec2{-0.02f, -0.82f}, chest + Vec2{-0.55f, -0.30f}};
    rig.hull(plate, 6);
    rig.detailLimb(chest + Vec2{0.28f, 0.52f}, chest + Vec2{0.62f, 0.10f}, 0.05f, 0.05f, kLayerHot);
    rig.detailLimb(chest + Vec2{0.62f, 0.10f}, chest + Vec2{0.48f, -0.42f}, 0.05f, 0.05f, kLayerHot);
    rig.detailLimb(hornBase, hornTip, 0.09f, 0.0f, kLayerHot);
    rig.detailLimb(head + Vec2{0.12f, -0.10f}, lowerHornTip, 0.08f, 0.0f, kLayerHot);
    rig.detailBlob(head + Vec2{0.14f, 0.0f}, 0.09f, 0.08f, 4);
    rig.endGroup();

    bentLimb(rig, chest + Vec2{-0.02f, -0.06f}, chest + Vec2{0.10f, -0.62f - s.armDrop}, chest + Vec2{0.58f, -0.72f - s.armDrop}, 0.19f, 0.15f, 0.02f);
    rig.endGroup();
}

void poseBoss(Rig& rig, float t, int phase) {
    const float phi = t * kTau;
    const float wave = std::abs(std::sin(phi));
    const float y = 0.10f * (1.0f - (1.0f - wave) * (1.0f - wave)) - 0.04f;
    const float swing = 0.16f * std::sin(phi);
    const Vec2 nearFoot = footPath(0.50f, phi, 0.34f, 0.26f);
    const Vec2 farFoot = footPath(-0.50f, phi + kPi, 0.34f, 0.26f);
    const Vec2 nearHip{0.48f, 1.12f + y};
    const Vec2 farHip{-0.48f, 1.12f + y};
    const Vec2 head{0.26f, 3.24f + y};

    // Far side.
    bentLeg(rig, farHip, farFoot, 0.10f, 0.44f, 0.36f, 0.30f);
    bentLimb(rig, Vec2{-1.20f, 2.42f + y}, Vec2{-1.80f - swing, 1.70f + y}, Vec2{-1.85f - swing, 1.02f + y}, 0.40f, 0.32f, 0.28f);
    rig.blob(Vec2{-1.86f - swing, 0.86f + y}, 0.38f, 0.36f, 8);
    rig.spike(Vec2{-2.05f - swing, 0.70f + y}, Vec2{-2.25f - swing, 0.20f + y}, 0.12f);
    rig.spike(Vec2{-1.75f - swing, 0.60f + y}, Vec2{-1.70f - swing, 0.10f + y}, 0.12f);
    if (phase >= 2) {
        // The last phase grows a second pair of arms, raised over its head.
        bentLimb(rig, Vec2{-0.95f, 2.75f + y}, Vec2{-1.62f, 3.30f + y + swing}, Vec2{-1.10f, 4.02f + y + swing}, 0.26f, 0.22f, 0.04f);
        bentLimb(rig, Vec2{1.05f, 2.75f + y}, Vec2{1.75f, 3.34f + y - swing}, Vec2{1.40f, 4.08f + y - swing}, 0.26f, 0.22f, 0.04f);
    }
    rig.endGroup();

    // Body.
    bentLeg(rig, nearHip, nearFoot, 0.12f, 0.46f, 0.38f, 0.32f);
    rig.blob(head, 0.36f, 0.32f, 8);
    rig.spike(head + Vec2{0.14f, -0.14f}, head + Vec2{0.72f, -0.40f}, 0.15f);
    const int crownSpikes = 3 + phase * 2;
    for (int i = 0; i < crownSpikes; ++i) {
        const float spread = crownSpikes > 1 ? static_cast<float>(i) / static_cast<float>(crownSpikes - 1) * 2.0f - 1.0f : 0.0f;
        const Vec2 base = head + Vec2{spread * 0.30f, 0.20f};
        rig.spike(base, base + Vec2{spread * 0.34f, 0.46f + 0.14f * static_cast<float>(phase)}, 0.09f);
    }
    const Vec2 body[8] = {Vec2{-1.46f, 2.20f + y}, Vec2{-1.25f, 2.78f + y}, Vec2{-0.35f, 3.05f + y}, Vec2{0.62f, 3.05f + y},
                          Vec2{1.36f, 2.78f + y},  Vec2{1.56f, 2.20f + y},  Vec2{0.72f, 1.02f + y},  Vec2{-0.66f, 1.02f + y}};
    rig.hull(body, 8);
    rig.spike(Vec2{-1.24f, 2.70f + y}, Vec2{-1.78f, 3.28f + y}, 0.24f);
    rig.spike(Vec2{1.36f, 2.72f + y}, Vec2{1.92f, 3.32f + y}, 0.24f);
    if (phase >= 1) {
        rig.spike(Vec2{-0.75f, 2.90f + y}, Vec2{-1.05f, 3.50f + y}, 0.20f);
        rig.spike(Vec2{0.95f, 2.90f + y}, Vec2{1.28f, 3.54f + y}, 0.20f);
    }
    rig.detailBlob(head + Vec2{0.13f, 0.04f}, 0.09f, 0.07f, 4);

    // Each phase breaks the chest open further, so the look shows the fight's progress.
    const Vec2 core{0.05f, 2.15f + y};
    if (phase == 0) {
        rig.detailLimb(Vec2{-0.85f, 2.42f + y}, Vec2{0.95f, 2.42f + y}, 0.035f, 0.035f, kLayerOutline);
        rig.detailLimb(Vec2{-0.62f, 1.82f + y}, Vec2{0.70f, 1.82f + y}, 0.035f, 0.035f, kLayerOutline);
        rig.detailLimb(Vec2{0.05f, 2.82f + y}, Vec2{0.05f, 1.28f + y}, 0.035f, 0.035f, kLayerOutline);
    } else if (phase == 1) {
        rig.detailBlob(core, 0.32f, 0.30f, 8);
        rig.detailBlob(core, 0.15f, 0.14f, 6, kLayerHot);
        rig.detailLimb(core + Vec2{0.30f, 0.15f}, Vec2{0.92f, 2.72f + y}, 0.05f, 0.01f, kLayerOutline);
        rig.detailLimb(core + Vec2{-0.30f, -0.12f}, Vec2{-0.85f, 1.62f + y}, 0.05f, 0.01f, kLayerOutline);
        rig.detailLimb(core + Vec2{-0.22f, 0.22f}, Vec2{-0.72f, 2.74f + y}, 0.05f, 0.01f, kLayerOutline);
    } else {
        rig.detailBlob(core, 0.58f, 0.54f, 10);
        rig.detailBlob(core, 0.34f, 0.32f, 8, kLayerHot);
        for (int i = 0; i < 3; ++i) {
            const float offset = static_cast<float>(i) * 0.26f;
            rig.detailLimb(Vec2{-0.85f, 2.72f + y - offset}, Vec2{-0.42f, 2.42f + y - offset}, 0.05f, 0.03f, kLayerOutline);
            rig.detailLimb(Vec2{0.98f, 2.72f + y - offset}, Vec2{0.52f, 2.42f + y - offset}, 0.05f, 0.03f, kLayerOutline);
        }
    }
    rig.endGroup();

    // Near arm.
    bentLimb(rig, Vec2{1.32f, 2.42f + y}, Vec2{1.92f + swing, 1.70f + y}, Vec2{1.96f + swing, 1.02f + y}, 0.42f, 0.34f, 0.30f);
    rig.blob(Vec2{1.98f + swing, 0.84f + y}, 0.42f, 0.38f, 8);
    rig.spike(Vec2{2.22f + swing, 0.72f + y}, Vec2{2.56f + swing, 0.32f + y}, 0.13f);
    rig.spike(Vec2{2.00f + swing, 0.55f + y}, Vec2{2.12f + swing, 0.04f + y}, 0.13f);
    rig.spike(Vec2{1.76f + swing, 0.58f + y}, Vec2{1.68f + swing, 0.08f + y}, 0.13f);
    rig.endGroup();
}

void buildPose(FigureId id, Rig& rig, float t, bool detailed) {
    switch (id) {
        case FigureId::Walker: poseWalker(rig, t, detailed); break;
        case FigureId::Runner: poseRunner(rig, t, detailed); break;
        case FigureId::Brute: poseBrute(rig, t, detailed); break;
        case FigureId::Spitter: poseSpitter(rig, t, detailed); break;
        case FigureId::Bloater: poseBloater(rig, t, detailed); break;
        case FigureId::Crawler: poseCrawler(rig, t, detailed); break;
        case FigureId::Charger: poseCharger(rig, t, detailed, ChargerStance{0.55f, 0.06f, 0.50f, 0.26f, 0.0f}); break;
        // Rears back and paws the ground, so the coming dash can be seen in the pose.
        case FigureId::ChargerBrace: poseCharger(rig, t, detailed, ChargerStance{-0.42f, 0.22f, 0.14f, 0.20f, -0.20f}); break;
        case FigureId::ChargerDash: poseCharger(rig, t, detailed, ChargerStance{1.40f, 0.36f, 0.92f, 0.55f, -0.10f}); break;
        case FigureId::ChargerRecover: poseCharger(rig, t, detailed, ChargerStance{0.25f, 0.30f, 0.12f, 0.08f, 0.40f}); break;
        case FigureId::BossPhase1: poseBoss(rig, t, 0); break;
        case FigureId::BossPhase2: poseBoss(rig, t, 1); break;
        case FigureId::BossPhase3: poseBoss(rig, t, 2); break;
        case FigureId::Count: break;
    }
}

float outlineFor(FigureId id) {
    switch (id) {
        case FigureId::Walker: return kWalkerLine / radiusOf(EnemyType::Walker);
        case FigureId::Runner: return kRunnerLine / radiusOf(EnemyType::Runner);
        case FigureId::Brute: return kBruteLine / radiusOf(EnemyType::Brute);
        case FigureId::Spitter: return kSpitterLine / radiusOf(EnemyType::Spitter);
        case FigureId::Bloater: return kBloaterLine / radiusOf(EnemyType::Bloater);
        case FigureId::Crawler: return kCrawlerLine / radiusOf(EnemyType::Crawler);
        case FigureId::Charger:
        case FigureId::ChargerBrace:
        case FigureId::ChargerDash:
        case FigureId::ChargerRecover: return kChargerLine / radiusOf(EnemyType::Charger);
        default: return kBossLine / radiusOf(EnemyType::Boss);
    }
}

struct BakedFigures {
    FigureQuad quads[kMaxBakedQuads];
    int start[static_cast<int>(FigureId::Count)][2][kFigureFrames];
    int count[static_cast<int>(FigureId::Count)][2][kFigureFrames];

    BakedFigures() {
        int used = 0;
        for (int id = 0; id < static_cast<int>(FigureId::Count); ++id) {
            const FigureId figure = static_cast<FigureId>(id);
            for (int frame = 0; frame < kFigureFrames; ++frame) {
                const float t = static_cast<float>(frame) / static_cast<float>(kFigureFrames);
                Rig full(quads + used, kMaxBakedQuads - used, outlineFor(figure));
                buildPose(figure, full, t, true);
                start[id][kFull][frame] = used;
                count[id][kFull][frame] = full.count();
                used += full.count();

                // The boss and a Charger mid-attack never use the cheap figure, as
                // those must stay readable. They share the full one, to save memory.
                start[id][kCheap][frame] = start[id][kFull][frame];
                count[id][kCheap][frame] = count[id][kFull][frame];
                if (id < static_cast<int>(FigureId::ChargerBrace)) {
                    Rig cheap(quads + used, kMaxBakedQuads - used, outlineFor(figure));
                    buildPose(figure, cheap, t, false);
                    start[id][kCheap][frame] = used;
                    count[id][kCheap][frame] = cheap.count();
                    used += cheap.count();
                }
            }
        }
    }
};

// Built before main runs, in fixed memory that lasts for the whole game.
const BakedFigures g_figures;

}  // namespace

FigureFrame figureFrame(FigureId id, bool detailed, int frame) {
    const int figure = static_cast<int>(id);
    const int level = detailed ? kFull : kCheap;
    return {g_figures.quads + g_figures.start[figure][level][frame], g_figures.count[figure][level][frame]};
}

}  // namespace ls::worldview
