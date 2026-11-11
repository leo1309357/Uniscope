/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RigOverlay2D.h"

namespace uniscope::d2 {

int RigOverlay2D::configure(const RigOverlayChannel2D* channels, int count) {
    channelCount_ = 0;
    if (channels == nullptr || count <= 0) {
        return 0;
    }
    for (int i = 0; i < count && channelCount_ < kMaxChannels; ++i) {
        if (channels[i].bone < 0) {
            continue;
        }
        channels_[channelCount_] = channels[i];
        ++channelCount_;
    }
    return channelCount_;
}

void RigOverlay2D::apply(float input, Skeleton2D& skeleton) const {
    if (channelCount_ == 0 || input == 0.0f) {
        return;
    }
    bool touched = false;
    for (int i = 0; i < channelCount_; ++i) {
        const RigOverlayChannel2D& channel = channels_[i];
        if (channel.bone < 0 || channel.bone >= static_cast<int>(skeleton.boneCount())) {
            continue;
        }
        float x = 0.0f;
        float y = 0.0f;
        float rotation = 0.0f;
        skeleton.localTransform(channel.bone, x, y, rotation);
        skeleton.setLocalTransform(channel.bone,
            x + input * channel.shiftX, y,
            rotation + input * channel.rotation);
        touched = true;
    }
    if (touched) {
        skeleton.solve();
    }
}

}  // namespace uniscope::d2
