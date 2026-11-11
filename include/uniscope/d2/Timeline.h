/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_TIMELINE_H
#define UNISCOPE_D2_TIMELINE_H

#include <functional>
#include <string>
#include <vector>

namespace uniscope::d2 {

struct TimelineEvent2D {
    float time = 0.0f;
    std::string track;
    std::function<void()> action;
};

class Timeline2D {
public:
    void addEvent(TimelineEvent2D event);
    void clear();
    // Fires events in (previousTime, currentTime]. Events exactly at the start
    // time do not fire; schedule them a hair past it, or fire entry events
    // directly when the state begins.
    void update(float previousTime, float currentTime);
    // Same, but wraps at `duration` when looping: fires the tail of the cycle
    // and then its head, which is what a looping gait needs for a footstep that
    // lands after the wrap.
    void updateWrapped(float previousTime, float currentTime, float duration,
                       bool looping);

private:
    std::vector<TimelineEvent2D> events_;
};

} // namespace uniscope::d2

#endif
