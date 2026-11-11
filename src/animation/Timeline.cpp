/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/Timeline.h"

namespace uniscope::d2 {

void Timeline2D::addEvent(TimelineEvent2D event) {
    events_.push_back(std::move(event));
}

void Timeline2D::clear() {
    events_.clear();
}

void Timeline2D::update(float previousTime, float currentTime) {
    for (const TimelineEvent2D& event : events_) {
        if (event.time > previousTime && event.time <= currentTime && event.action) {
            event.action();
        }
    }
}

void Timeline2D::updateWrapped(float previousTime, float currentTime,
                               float duration, bool looping) {
    if (looping && duration > 0.0f && currentTime < previousTime) {
        update(previousTime, duration);
        update(0.0f, currentTime);
        return;
    }
    update(previousTime, currentTime);
}

} // namespace uniscope::d2
