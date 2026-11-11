/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/AnimationPlayer.h"

#include <algorithm>
#include <cmath>

namespace uniscope::d2 {

namespace {

float shortestAngleDelta(float from, float to) {
    constexpr float kPi = 3.14159265359f;
    constexpr float kTwoPi = 6.28318530718f;
    float delta = to - from;
    while (delta > kPi) {
        delta -= kTwoPi;
    }
    while (delta < -kPi) {
        delta += kTwoPi;
    }
    return delta;
}

} // namespace

void AnimationPlayer2D::setTargetSkeleton(Skeleton2D* skeleton) {
    target_ = skeleton;
    ensureBuffers();
}

void AnimationPlayer2D::ensureBuffers() {
    const std::size_t count = target_ != nullptr ? target_->boneCount() : 0;
    if (clipX_.size() == count) {
        return;
    }
    clipX_.assign(count, 0.0f);
    clipY_.assign(count, 0.0f);
    clipRot_.assign(count, 0.0f);
    clipScaleX_.assign(count, 1.0f);
    clipScaleY_.assign(count, 1.0f);
    prevX_ = clipX_;
    prevY_ = clipY_;
    prevRot_ = clipRot_;
    prevScaleX_ = clipScaleX_;
    prevScaleY_ = clipScaleY_;
}

void AnimationPlayer2D::play(const AnimationClip2D* clip, bool restart) {
    if (clip == nullptr) {
        return;
    }
    if (!restart && clip_ == clip) {
        // Re-selecting the active clip finishes any pending fade instead of
        // restarting it, which keeps rapid state churn from stuttering.
        if (blending_) {
            previous_ = clip;
            blend_ = 1.0f;
            blending_ = false;
        }
        return;
    }
    clip_ = clip;
    previous_ = nullptr;
    previousTime_ = time_;
    time_ = 0.0f;
    blend_ = 1.0f;
    blending_ = false;
}

void AnimationPlayer2D::crossFade(const AnimationClip2D* clip, float blendSeconds) {
    if (clip == nullptr) {
        return;
    }
    if (blendSeconds <= 0.0f || clip_ == nullptr) {
        play(clip, true);
        return;
    }
    if (clip_ == clip) {
        return;
    }

    previous_ = clip_;
    previousTime_ = time_;
    clip_ = clip;
    time_ = 0.0f;
    blend_ = 0.0f;
    blendRate_ = 1.0f / blendSeconds;
    blending_ = true;
}

float AnimationPlayer2D::normalizedTime() const {
    if (clip_ == nullptr || clip_->duration() <= 0.0f) {
        return 0.0f;
    }
    return time_ / clip_->duration();
}

void AnimationPlayer2D::setNormalizedTime(float phase) {
    if (clip_ == nullptr || clip_->duration() <= 0.0f) {
        return;
    }
    const float clamped = std::max(0.0f, std::min(1.0f, phase));
    time_ = clamped * clip_->duration();
    if (clip_->looping() && time_ >= clip_->duration()) {
        time_ = std::fmod(time_, clip_->duration());
    }
}

bool AnimationPlayer2D::finished() const {
    if (clip_ == nullptr) {
        return true;
    }
    if (clip_->looping()) {
        return false;
    }
    return time_ >= clip_->duration();
}

void AnimationPlayer2D::update(float deltaSeconds) {
    if (deltaSeconds <= 0.0f) {
        return;
    }
    const float step = deltaSeconds * timeScale_;

    if (clip_ != nullptr) {
        time_ += step;
        if (clip_->duration() > 0.0f) {
            time_ = clip_->looping() ? std::fmod(time_, clip_->duration())
                                     : std::min(time_, clip_->duration());
        }
    }
    if (previous_ != nullptr) {
        previousTime_ += step;
        if (previous_->duration() > 0.0f) {
            previousTime_ = previous_->looping() ? std::fmod(previousTime_, previous_->duration())
                                                : std::min(previousTime_, previous_->duration());
        }
    }

    if (blending_) {
        blend_ = std::min(1.0f, blend_ + blendRate_ * deltaSeconds);
        if (blend_ >= 1.0f) {
            blending_ = false;
            previous_ = nullptr;
        }
    }

    writePose();
}

void AnimationPlayer2D::writePose() {
    if (target_ == nullptr) {
        return;
    }
    const std::size_t count = target_->boneCount();
    if (clipX_.size() != count || count == 0) {
        ensureBuffers();
        if (clipX_.size() != count) {
            return;
        }
    }

    // Seed both buffers from the bind pose before sampling. A clip only keys the
    // joints it actually moves; seeding means every other joint holds its rest
    // transform instead of collapsing onto the rig origin, which is what would
    // otherwise happen for a clip that keys a handful of bones.
    //
    // From `bindPose()`, not from the bone's current local transform: the current
    // transform is whatever the *previous* clip last wrote, so a bone keyed by one
    // clip and not the next would inherit the first clip's pose instead of its
    // rest pose.
    const Skeleton2D& skeleton = *target_;
    for (std::size_t index = 0; index < count; ++index) {
        const BoneBind2D& rest = skeleton.bindPose(static_cast<int>(index));
        clipX_[index] = rest.x;
        clipY_[index] = rest.y;
        clipRot_[index] = rest.rotation;
        clipScaleX_[index] = rest.scaleX;
        clipScaleY_[index] = rest.scaleY;
        prevX_[index] = rest.x;
        prevY_[index] = rest.y;
        prevRot_[index] = rest.rotation;
        prevScaleX_[index] = rest.scaleX;
        prevScaleY_[index] = rest.scaleY;
    }

    if (clip_ != nullptr) {
        clip_->sample(time_, clipX_, clipY_, clipRot_, clipScaleX_, clipScaleY_);
    }
    if (previous_ != nullptr) {
        previous_->sample(previousTime_, prevX_, prevY_, prevRot_, prevScaleX_, prevScaleY_);
    }

    const float weight = std::clamp(blend_, 0.0f, 1.0f);
    for (std::size_t index = 0; index < count; ++index) {
        if (clip_ == nullptr) {
            break;
        }
        float x = clipX_[index];
        float y = clipY_[index];
        float rotation = clipRot_[index];
        float scaleX = clipScaleX_[index];
        float scaleY = clipScaleY_[index];

        if (previous_ != nullptr && weight < 1.0f) {
            const float inverse = 1.0f - weight;
            x = prevX_[index] * inverse + x * weight;
            y = prevY_[index] * inverse + y * weight;
            // Blend along the short arc: a naive lerp spins the long way
            // around whenever the two poses straddle +-pi.
            rotation = prevRot_[index] + shortestAngleDelta(prevRot_[index], rotation) * weight;
            scaleX = prevScaleX_[index] * inverse + scaleX * weight;
            scaleY = prevScaleY_[index] * inverse + scaleY * weight;
        }
        target_->setLocalTransform(static_cast<int>(index), x, y, rotation);
        target_->setLocalScale(static_cast<int>(index), scaleX, scaleY);
    }
    target_->solve();
}

void AnimationPlayer2D::reset() {
    time_ = 0.0f;
    previousTime_ = 0.0f;
    blend_ = 1.0f;
    blending_ = false;
    previous_ = nullptr;
    if (target_ != nullptr) {
        target_->resetToRestPose();
        target_->solve();
    }
}

const Skeleton2D& AnimationPlayer2D::pose() const {
    if (target_ != nullptr) {
        return *target_;
    }
    return identity_;
}

} // namespace uniscope::d2
