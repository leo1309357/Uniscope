/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/AnimationClip.h"

#include <algorithm>
#include <cmath>

namespace uniscope::d2 {

namespace {

float ease(float t, KeyframeCurve curve) {
    if (curve == KeyframeCurve::Step) {
        return 0.0f;
    }
    if (curve == KeyframeCurve::Linear) {
        return t;
    }
    return t * t * (3.0f - 2.0f * t);
}

float shortestAngleDelta(float from, float to) {
    // Wrapped to +-pi (not +-2pi): anything larger takes the long way around,
    // so a 270-degree swing would rotate forward instead of 90 back.
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

void AnimationClip2D::addKeyframe(BoneKeyframe2D keyframe) {
    duration_ = std::max(duration_, keyframe.time);
    keyframes_.push_back(keyframe);
    sorted_ = false;
}

void AnimationClip2D::sort() {
    // Fold keyframe times into one period first, for a looping clip.
    //
    // Authoring a cyclic track by phase - the right leg of a walk starting half a
    // stride after the left, say - naturally produces keys at t, t + duration,
    // t + 2 * duration, and so on. sample() wraps its time into [0, duration), so
    // every key at or past the duration was simply unreachable: the track held its
    // first key for the whole stretch before that key's time and squeezed the rest
    // of the cycle into what was left. For the walk that meant the right leg stood
    // frozen forward for half of every stride and then swung through all of its
    // phases in the other half, so the two legs were never half a cycle apart and
    // the stride read as a lunge.
    //
    // Folding is what makes the author's intent the sampled result. It has to happen
    // after the final duration is known and before the ordering pass, since folding
    // changes the sort key.
    //
    // Only for looping clips. A one-shot holds its last key by design, so a key past
    // the end there is a genuine authoring mistake rather than a wrapped cycle, and
    // silently relocating it would hide it.
    if (looping_ && duration_ > 0.0f) {
        for (BoneKeyframe2D& keyframe : keyframes_) {
            float time = std::fmod(keyframe.time, duration_);
            if (time < 0.0f) {
                time += duration_;
            }
            keyframe.time = time;
        }
    }

    std::stable_sort(
        keyframes_.begin(),
        keyframes_.end(),
        [](const BoneKeyframe2D& left, const BoneKeyframe2D& right) {
            if (left.boneIndex != right.boneIndex) {
                return left.boneIndex < right.boneIndex;
            }
            return left.time < right.time;
        });

    sorted_ = true;
}

void AnimationClip2D::sample(
    float time,
    std::vector<float>& outX,
    std::vector<float>& outY,
    std::vector<float>& outRotation,
    std::vector<float>& outScaleX,
    std::vector<float>& outScaleY) const {
    if (keyframes_.empty()) {
        return;
    }
    if (duration_ > 0.0f) {
        time = looping_
            ? std::fmod(time, duration_)
            : std::clamp(time, 0.0f, duration_);
        if (time < 0.0f) {
            time += duration_;
        }
    }

    for (std::size_t index = 0; index < keyframes_.size();) {
        const int boneIndex = keyframes_[index].boneIndex;
        std::size_t end = index;
        while (end < keyframes_.size() && keyframes_[end].boneIndex == boneIndex) {
            ++end;
        }
        if (boneIndex < 0 || boneIndex >= static_cast<int>(outX.size())) {
            index = end;
            continue;
        }

        const BoneKeyframe2D& first = keyframes_[index];
        if (time <= first.time) {
            outX[static_cast<std::size_t>(boneIndex)] = first.x;
            outY[static_cast<std::size_t>(boneIndex)] = first.y;
            outRotation[static_cast<std::size_t>(boneIndex)] = first.rotation;
            outScaleX[static_cast<std::size_t>(boneIndex)] = first.scaleX;
            outScaleY[static_cast<std::size_t>(boneIndex)] = first.scaleY;
            index = end;
            continue;
        }

        const BoneKeyframe2D& last = keyframes_[end - 1];
        if (time >= last.time) {
            // Past the final key the clip either holds or wraps back to the
            // first key so a cyclic walk loop does not pop.
            if (!looping_ || end - index < 2) {
                outX[static_cast<std::size_t>(boneIndex)] = last.x;
                outY[static_cast<std::size_t>(boneIndex)] = last.y;
                outRotation[static_cast<std::size_t>(boneIndex)] = last.rotation;
                outScaleX[static_cast<std::size_t>(boneIndex)] = last.scaleX;
                outScaleY[static_cast<std::size_t>(boneIndex)] = last.scaleY;
                index = end;
                continue;
            }
        }

        std::size_t next = index;
        while (next + 1 < end && keyframes_[next + 1].time <= time) {
            ++next;
        }
        const BoneKeyframe2D& a = keyframes_[next];
        const BoneKeyframe2D& b = keyframes_[next + 1 < end ? next + 1 : index];
        const float span = b.time - a.time;
        const float raw = span > 0.0001f ? (time - a.time) / span : 0.0f;
        const float t = ease(std::clamp(raw, 0.0f, 1.0f), a.curve);

        const std::size_t slot = static_cast<std::size_t>(boneIndex);
        outX[slot] = a.x + (b.x - a.x) * t;
        outY[slot] = a.y + (b.y - a.y) * t;
        outRotation[slot] = a.rotation + shortestAngleDelta(a.rotation, b.rotation) * t;
        outScaleX[slot] = a.scaleX + (b.scaleX - a.scaleX) * t;
        outScaleY[slot] = a.scaleY + (b.scaleY - a.scaleY) * t;
        index = end;
    }
}

} // namespace uniscope::d2
