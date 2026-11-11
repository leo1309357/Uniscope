/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RigClipSet2D.h"

#include <algorithm>

namespace uniscope::d2 {

void RigClipSet2D::configure(int stateCount, int viewCount) {
    stateCount_ = std::max(1, stateCount);
    viewCount_ = std::max(1, viewCount);
    player_.reset();
    clips_.assign(static_cast<std::size_t>(stateCount_ * viewCount_),
                  AnimationClip2D{});
}

AnimationClip2D& RigClipSet2D::clipOrFirst(int state, int view) {
    if (state < 0 || state >= stateCount_ || view < 0 || view >= viewCount_) {
        return clips_.front();
    }
    return clips_[static_cast<std::size_t>(state * viewCount_ + view)];
}

AnimationClip2D& RigClipSet2D::clip(int state, int view) {
    return clipOrFirst(state, view);
}

const AnimationClip2D& RigClipSet2D::clip(int state, int view) const {
    if (state < 0 || state >= stateCount_ || view < 0 || view >= viewCount_) {
        return clips_.front();
    }
    return clips_[static_cast<std::size_t>(state * viewCount_ + view)];
}

void RigClipSet2D::play(int state, int view, bool restart) {
    if (!valid()) {
        return;
    }
    player_.play(&clip(state, view), restart);
}

void RigClipSet2D::crossFade(int state, int view, float blendSeconds) {
    if (!valid()) {
        return;
    }
    player_.crossFade(&clip(state, view), blendSeconds);
}

void RigClipSet2D::crossFadeKeepPhase(int state, int view, float blendSeconds) {
    if (!valid()) {
        return;
    }
    const float phase = player_.normalizedTime();
    player_.crossFade(&clip(state, view), blendSeconds);
    player_.setNormalizedTime(phase);
}

}  // namespace uniscope::d2
