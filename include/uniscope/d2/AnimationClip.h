/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ANIMATION_CLIP_H
#define UNISCOPE_D2_ANIMATION_CLIP_H

#include <cstdint>
#include <string>
#include <vector>

namespace uniscope::d2 {

enum class KeyframeCurve : std::uint8_t {
    Step,     // holds the previous value until the next key
    Linear,
    Smooth,   // smoothstep between keys; the default for authored motion
};

// One keyed channel sample for a bone. `x`/`y`/`rotation` replace the bone's
// rest values, `scaleX`/`scaleY` drive squash-and-stretch.
struct BoneKeyframe2D {
    float time = 0.0f;
    int boneIndex = 0;
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    KeyframeCurve curve = KeyframeCurve::Smooth;
};

// A named animation clip: a bundle of per-bone keyframes sampled into a pose.
class AnimationClip2D {
public:
    void addKeyframe(BoneKeyframe2D keyframe);

    // Folds looping keyframe times into one period and orders them per bone. Must
    // be called after the final `setDuration`, because the fold uses it. Called
    // once the tracks are authored; exposed for bulk authoring.
    void sort();

    void setDuration(float duration) { duration_ = duration; }
    void setLooping(bool looping) { looping_ = looping; }
    bool looping() const { return looping_; }
    void setName(std::string name) { name_ = std::move(name); }
    const std::string& name() const { return name_; }

    float duration() const { return duration_; }

    // Samples every animated channel at `time` and writes the results into
    // `outValues`, which is indexed by bone index and pre-sized to the target
    // skeleton. `outRotations` and the scale array behave the same way.
    void sample(
        float time,
        std::vector<float>& outX,
        std::vector<float>& outY,
        std::vector<float>& outRotation,
        std::vector<float>& outScaleX,
        std::vector<float>& outScaleY) const;

private:
    std::vector<BoneKeyframe2D> keyframes_;
    std::string name_;
    float duration_ = 0.0f;
    bool looping_ = true;
    bool sorted_ = true;
};

} // namespace uniscope::d2

#endif
