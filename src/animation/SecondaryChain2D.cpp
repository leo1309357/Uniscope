/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/SecondaryChain2D.h"

#include <algorithm>
#include <cmath>

namespace uniscope::d2 {

namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kTwoPi = 2.0f * kPi;

// Mechanism constants: clamp bands, coupling and the substep count. These
// shape the integrator, not the content, so they live here rather than in the
// caller's parameter table.
constexpr float kMaxTheta = 1.0f;
constexpr float kMaxOmega = 10.0f;
constexpr float kMaxApplied = 1.0f;
constexpr float kCouple = 6.0f;
constexpr int kSubsteps = 2;
constexpr float kMaxStep = 0.05f;

}  // namespace

bool SecondaryChain2D::configure(
    const Skeleton2D& rest, const int* linkBones, int linkCount, int pivotBone) {
    linkCount_ = 0;
    pivotBone_ = -1;
    if (linkBones == nullptr || linkCount <= 0 || linkCount > kMaxLinks
        || pivotBone < 0 || pivotBone >= static_cast<int>(rest.boneCount())) {
        return false;
    }
    for (int i = 0; i < linkCount; ++i) {
        const int bone = linkBones[i];
        if (bone < 0 || bone >= static_cast<int>(rest.boneCount())) {
            return false;
        }
        linkBones_[i] = bone;
        bindRotation_[i] = rest.bindPose(bone).rotation;
        theta_[i] = 0.0f;
        omega_[i] = 0.0f;
        applied_[i] = 0.0f;
    }
    for (int i = linkCount; i < kMaxLinks; ++i) {
        linkBones_[i] = -1;
        bindRotation_[i] = 0.0f;
        theta_[i] = 0.0f;
        omega_[i] = 0.0f;
        applied_[i] = 0.0f;
    }
    linkCount_ = linkCount;
    pivotBone_ = pivotBone;
    restWorldAngle_ = rest.worldTransform(linkBones[0]).rotation;
    reset();
    return true;
}

float SecondaryChain2D::angle(int link) const {
    if (link < 0 || link >= linkCount_) {
        return 0.0f;
    }
    const float angle = applied_[link];
    return std::isfinite(angle) ? angle : 0.0f;
}

void SecondaryChain2D::reset() {
    for (int i = 0; i < kMaxLinks; ++i) {
        theta_[i] = 0.0f;
        omega_[i] = 0.0f;
        applied_[i] = 0.0f;
    }
    time_ = 0.0f;
    stridePhase_ = 0.0f;
    bounceBlend_ = 0.0f;
    prevPivotX_ = 0.0f;
    prevPivotY_ = 0.0f;
    prevVelX_ = 0.0f;
    prevVelY_ = 0.0f;
    initialised_ = false;
}

void SecondaryChain2D::update(
    float deltaSeconds, Skeleton2D& skeleton,
    const SecondaryChainParams2D& params) {
    if (!valid()) {
        return;
    }
    // Clamp the step: a hitch (tab switch, debugger) must bend the chain, not
    // catapult it. Two semi-implicit Euler substeps keep a stiff Land spring
    // stable with margin.
    const float dt = std::clamp(deltaSeconds, 0.0f, kMaxStep);
    if (!(dt > 0.0f)) {
        return;
    }

    const BoneTransform2D& pivotWorld = skeleton.worldTransform(pivotBone_);
    if (!initialised_) {
        prevPivotX_ = pivotWorld.x;
        prevPivotY_ = pivotWorld.y;
        prevVelX_ = 0.0f;
        prevVelY_ = 0.0f;
        initialised_ = true;
        return;
    }

    // Pivot linear acceleration, finite-differenced and clamped. This is the
    // inertial drive: when the pivot lunges, the chain trails behind it.
    float velX = (pivotWorld.x - prevPivotX_) / dt;
    float velY = (pivotWorld.y - prevPivotY_) / dt;
    velX = std::clamp(velX, -6.0f, 6.0f);
    velY = std::clamp(velY, -6.0f, 6.0f);
    float accX = (velX - prevVelX_) / dt;
    float accY = (velY - prevVelY_) / dt;
    accX = std::clamp(accX, -30.0f, 30.0f);
    accY = std::clamp(accY, -30.0f, 30.0f);
    prevPivotX_ = pivotWorld.x;
    prevPivotY_ = pivotWorld.y;
    prevVelX_ = velX;
    prevVelY_ = velY;

    // Wind stirs the sway and pushes the chain downwind. Only a speed reaches
    // the rig (no heading in its side-view plane), so the push follows the
    // streaming direction: with the trail when moving, fading to nothing when
    // still (where only the sway stir remains). The tanh keeps the sign flip
    // smooth instead of popping.
    const float wind = std::clamp(params.wind, 0.0f, 12.0f);
    const float windSway = wind * 0.012f;

    // Pivot motion projected onto the chain's swing axis (perpendicular to its
    // rest world direction). Acceleration lunges and velocity drag both trail
    // the chain opposite the motion; the tanh bounds spikes.
    const float perpX = -std::sin(restWorldAngle_);
    const float perpY = std::cos(restWorldAngle_);
    const float accAlong = accX * perpX + accY * perpY;
    const float velAlong = velX * perpX + velY * perpY;
    const float swingBase = std::tanh(accAlong / 8.0f);
    const float trailBase = std::tanh(velAlong / 3.0f);
    const float windBiasAmp = wind * 0.010f;
    const float windBiasSign = -std::tanh(trailBase * 6.0f);

    time_ += dt;
    // Stride-locked bounce: the spring above (~1 Hz) cannot follow footfall
    // bob, so bounce is applied directly, phase-travelled down the chain.
    stridePhase_ += kTwoPi * std::max(0.0f, params.strideCyclesPerSecond) * dt;
    const float wantBounce = params.bounce > 0.0f ? 1.0f : 0.0f;
    bounceBlend_ += (wantBounce - bounceBlend_) * std::min(1.0f, dt * 6.0f);
    if (std::fabs(bounceBlend_) < 0.001f) {
        bounceBlend_ = 0.0f;
    }

    const float h = dt / static_cast<float>(kSubsteps);
    for (int sub = 0; sub < kSubsteps; ++sub) {
        for (int i = 0; i < linkCount_; ++i) {
            const float growth = 1.0f + 0.3f * static_cast<float>(i);
            // Sway target grows and phase-travels down the chain; the tip
            // dances more than the root, the way a real plait does.
            const float swayTarget =
                std::sin(time_ * params.swayHz * kTwoPi + static_cast<float>(i) * 0.9f)
                    * (params.swayAmp + windSway) * (1.0f + 0.35f * static_cast<float>(i))
                + windBiasAmp * windBiasSign * (1.0f + 0.4f * static_cast<float>(i));
            // Targets trail opposite the motion: a lunging pivot leaves the
            // chain behind it, steady travel streams it backwards.
            const float target = swayTarget
                - swingBase * params.swing * growth
                - trailBase * params.trail * growth;
            const float neighbour = (i == 0) ? 0.0f : theta_[i - 1];
            const float acc = -params.stiffness * (theta_[i] - target)
                - params.damping * omega_[i]
                + kCouple * (neighbour - theta_[i]);
            omega_[i] = std::clamp(omega_[i] + acc * h, -kMaxOmega, kMaxOmega);
            theta_[i] = std::clamp(theta_[i] + omega_[i] * h, -kMaxTheta, kMaxTheta);
            if (!std::isfinite(theta_[i]) || !std::isfinite(omega_[i])) {
                theta_[i] = 0.0f;
                omega_[i] = 0.0f;
            }
        }
    }

    // Absolute write-back (bind + spring + bounce). The clips never key these
    // bones, so no sampling pass rewrites them; an incremental overlay would
    // integrate without bound.
    for (int i = 0; i < linkCount_; ++i) {
        const float bounce = bounceBlend_ * params.bounce
            * std::sin(stridePhase_ + static_cast<float>(i) * 0.6f)
            * (1.0f + 0.3f * static_cast<float>(i));
        applied_[i] = std::clamp(theta_[i] + bounce, -kMaxApplied, kMaxApplied);
        if (!std::isfinite(applied_[i])) {
            applied_[i] = 0.0f;
        }
        float x = 0.0f;
        float y = 0.0f;
        float rotation = 0.0f;
        skeleton.localTransform(linkBones_[i], x, y, rotation);
        skeleton.setLocalTransform(
            linkBones_[i], x, y, bindRotation_[i] + applied_[i]);
    }
    skeleton.solve();
}

}  // namespace uniscope::d2
