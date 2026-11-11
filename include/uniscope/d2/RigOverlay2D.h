/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RIGOVERLAY2D_H
#define UNISCOPE_D2_RIGOVERLAY2D_H

#include "Skeleton.h"

namespace uniscope::d2 {

// One bone's response to a scalar pose input (a weight shift, a hit reaction,
// a wind gust): a local X offset and a local rotation, both in units per unit
// of input.
struct RigOverlayChannel2D {
    int bone = -1;
    float shiftX = 0.0f;
    float rotation = 0.0f;
};

// Additive pose overlay driven by one scalar.
//
// The clips rewrite every keyed local transform each frame and then solve, so
// an overlay must be applied *after* sampling and must not accumulate: this
// writes current-local + input * coefficient every call. The channel table is
// content and stays with the character (hips lead, chest counter-rotates);
// the library owns the traversal, the negative-bone skip and the re-solve.
class RigOverlay2D {
public:
    static constexpr int kMaxChannels = 8;

    // Copies up to kMaxChannels channels. Negative-bone rows are dropped, so a
    // table may be built before the skeleton exists or for a rig without an
    // optional bone. Returns the number of bound channels.
    int configure(const RigOverlayChannel2D* channels, int count);

    int channelCount() const { return channelCount_; }
    bool valid() const { return channelCount_ > 0; }

    // Applies input * coefficient on top of the current local pose, then
    // re-solves. Zero input leaves the pose untouched (no pointless solve).
    void apply(float input, Skeleton2D& skeleton) const;

private:
    RigOverlayChannel2D channels_[kMaxChannels] = {};
    int channelCount_ = 0;
};

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_RIGOVERLAY2D_H
