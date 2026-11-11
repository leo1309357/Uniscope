/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RIGCLIPSET2D_H
#define UNISCOPE_D2_RIGCLIPSET2D_H

#include "AnimationClip.h"
#include "AnimationPlayer.h"
#include "Skeleton.h"

#include <vector>

namespace uniscope::d2 {

// A state x view pool of clips plus the player that plays them.
//
// A character layer keeps its own state/view enums and authors the clips; this
// owns the mechanism every animated rig needs around them: stable clip storage,
// index -> clip lookup, and the three transition shapes (hard switch,
// cross-fade, cross-fade that keeps the gait phase when only the view changes).
// The fade *durations* and the time-scale rule stay with the caller - they are
// content, not mechanism.
class RigClipSet2D {
public:
    // Allocates stateCount * viewCount clips and resets the player. Re-calling
    // this drops every clip, so do it before the builders run, not mid-play.
    void configure(int stateCount, int viewCount);

    int stateCount() const { return stateCount_; }
    int viewCount() const { return viewCount_; }
    bool valid() const { return !clips_.empty() && stateCount_ > 0 && viewCount_ > 0; }

    // Index = state * viewCount + view. Out-of-range reads fall back to slot 0,
    // so authoring code cannot scribble outside the pool.
    AnimationClip2D& clip(int state, int view);
    const AnimationClip2D& clip(int state, int view) const;

    AnimationPlayer2D& player() { return player_; }
    const AnimationPlayer2D& player() const { return player_; }

    const AnimationClip2D* currentClip() const { return player_.clip(); }
    float currentTime() const { return player_.time(); }
    float normalizedTime() const { return player_.normalizedTime(); }
    bool blending() const { return player_.blending(); }

    void setTargetSkeleton(Skeleton2D* skeleton) { player_.setTargetSkeleton(skeleton); }

    // Hard switch; `restart` rewinds when the same clip is already playing.
    void play(int state, int view, bool restart = false);
    // Blends into the clip over `blendSeconds`; the incoming clip starts at 0.
    void crossFade(int state, int view, float blendSeconds);
    // Blends into another view while keeping the current normalized phase - the
    // same gait with different keyframes, not a restart.
    void crossFadeKeepPhase(int state, int view, float blendSeconds);

    void setTimeScale(float timeScale) { player_.setTimeScale(timeScale); }
    void update(float deltaSeconds) { player_.update(deltaSeconds); }

private:
    AnimationClip2D& clipOrFirst(int state, int view);
    std::vector<AnimationClip2D> clips_;
    AnimationPlayer2D player_;
    int stateCount_ = 0;
    int viewCount_ = 0;
};

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_RIGCLIPSET2D_H
