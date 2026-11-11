/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_SECONDARYCHAIN2D_H
#define UNISCOPE_D2_SECONDARYCHAIN2D_H

#include "Skeleton.h"

namespace uniscope::d2 {

// Per-frame drive for a secondary chain. The character layer fills one of
// these from its own state table; the mechanism below is the same for a plait,
// a cape, an earring or a key ring.
struct SecondaryChainParams2D {
    // Spring: how hard a link is pulled toward its (animated) target, and how
    // fast it loses speed. A stiffer Land spring needs both, or the chain
    // wobbles through the landing.
    float stiffness = 12.0f;
    float damping = 4.0f;
    // Idle sway: a sine at this frequency and amplitude, phase-travelling down
    // the chain so the tip lags the root.
    float swayHz = 0.5f;
    float swayAmp = 0.0f;
    // Reaction to the pivot: acceleration lunges the chain behind the motion,
    // velocity streams it backwards. Both scale a bounded tanh, so a spike
    // cannot throw the chain.
    float swing = 0.0f;
    float trail = 0.0f;
    // Stride-locked bounce in radians, faded in/out with a short blend so a
    // state change does not pop. Zero disables it.
    float bounce = 0.0f;
    // Ambient wind in m/s (clamped to 0..12). It stirs the sway, and pushes
    // the chain along the streaming direction when the pivot is moving.
    float wind = 0.0f;
    // Bounce tempo in cycles per second. The caller derives it from its clip
    // rate and stride duration; the chain does not know about clips.
    float strideCyclesPerSecond = 0.0f;
};

// A damped multi-link pendulum hanging off a pivot bone.
//
// The pivot's linear acceleration and velocity are projected onto the chain's
// rest swing axis and drive every link; links couple to their parent, are
// clamped, and are written back as absolute local rotations (bind + spring +
// bounce) before the skeleton is re-solved. Rotation only: localX/localY
// belong to the clips, so the chain must never integrate them.
//
// The chain is decorative: clips must not key its bones, or sampling would
// overwrite the physical pose every frame.
class SecondaryChain2D {
public:
    static constexpr int kMaxLinks = 4;

    // Binds up to kMaxLinks bones, the pivot whose motion drives them, and
    // captures the rest pose from `rest` (which must be solved): each link's
    // bind local rotation and the root's rest world direction. Returns false
    // for a missing bone, an empty chain or a chain longer than kMaxLinks; on
    // failure the object is invalid and update() does nothing.
    bool configure(const Skeleton2D& rest, const int* linkBones, int linkCount, int pivotBone);

    bool valid() const { return linkCount_ > 0 && pivotBone_ >= 0; }
    int linkCount() const { return linkCount_; }

    // Applied rotation for a link, in radians (0 at rest), clamped to +-1.
    // Out-of-range links read 0, matching a chain that is not there.
    float angle(int link) const;

    // Drops the integrator state (used after a rebuild or a teleport) while
    // keeping the captured bind pose and the chain binding.
    void reset();

    // Advances the spring and writes absolute local rotations back into
    // `skeleton`, then re-solves it. The skeleton must already be posed for
    // this frame. Non-positive time steps are ignored, and a huge hitch is
    // clamped so the chain bends instead of catapulting.
    void update(float deltaSeconds, Skeleton2D& skeleton,
                const SecondaryChainParams2D& params);

private:
    int linkBones_[kMaxLinks] = {-1, -1, -1, -1};
    int linkCount_ = 0;
    int pivotBone_ = -1;
    float bindRotation_[kMaxLinks] = {0.0f, 0.0f, 0.0f, 0.0f};
    float restWorldAngle_ = 0.0f;
    float theta_[kMaxLinks] = {0.0f, 0.0f, 0.0f, 0.0f};
    float omega_[kMaxLinks] = {0.0f, 0.0f, 0.0f, 0.0f};
    float applied_[kMaxLinks] = {0.0f, 0.0f, 0.0f, 0.0f};
    float time_ = 0.0f;
    float stridePhase_ = 0.0f;
    float bounceBlend_ = 0.0f;
    float prevPivotX_ = 0.0f;
    float prevPivotY_ = 0.0f;
    float prevVelX_ = 0.0f;
    float prevVelY_ = 0.0f;
    bool initialised_ = false;
};

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_SECONDARYCHAIN2D_H
