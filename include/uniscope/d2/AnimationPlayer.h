/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ANIMATION_PLAYER_H
#define UNISCOPE_D2_ANIMATION_PLAYER_H

#include "uniscope/d2/AnimationClip.h"
#include "uniscope/d2/Skeleton.h"

namespace uniscope::d2 {

// Drives one or two clips into a target skeleton, with a cross-fade window so
// state changes (idle -> run -> jump) blend instead of snapping.
//
// Only the bones a clip actually keys are written; everything else keeps the
// rest pose captured on the target skeleton.
class AnimationPlayer2D {
public:
    // Skeleton whose local transforms this player writes. Not owned.
    void setTargetSkeleton(Skeleton2D* skeleton);

    // Starts a clip immediately. `restart` rewinds when the same clip is
    // already active.
    void play(const AnimationClip2D* clip, bool restart = false);

    // Blends into `clip` over `blendSeconds`. A non-positive duration is a
    // hard switch.
    void crossFade(const AnimationClip2D* clip, float blendSeconds);

    float blend() const { return blend_; }
    bool blending() const { return blending_; }

    void setTimeScale(float timeScale) { timeScale_ = timeScale > 0.0f ? timeScale : 1.0f; }

    void update(float deltaSeconds);
    void reset();

    float time() const { return time_; }
    float normalizedTime() const;
    // Rewinds or advances the active clip to `phase` (0..1, clamped), keeping
    // the same clip selected. Used to rebind a clip (e.g. on a facing change)
    // without losing the gait phase the previous clip was playing at.
    void setNormalizedTime(float phase);
    bool finished() const;

    const AnimationClip2D* clip() const { return clip_; }
    const AnimationClip2D* previousClip() const { return previous_; }
    const Skeleton2D& pose() const;

private:
    void writePose();
    void ensureBuffers();

    Skeleton2D* target_ = nullptr;
    Skeleton2D identity_;
    const AnimationClip2D* clip_ = nullptr;
    const AnimationClip2D* previous_ = nullptr;
    float time_ = 0.0f;
    float previousTime_ = 0.0f;
    float blend_ = 1.0f;
    float blendRate_ = 0.0f;
    float timeScale_ = 1.0f;
    bool blending_ = false;
    std::vector<float> clipX_, clipY_, clipRot_, clipScaleX_, clipScaleY_;
    std::vector<float> prevX_, prevY_, prevRot_, prevScaleX_, prevScaleY_;
};

} // namespace uniscope::d2

#endif
