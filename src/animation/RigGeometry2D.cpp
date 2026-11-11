/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RigGeometry2D.h"

#include <algorithm>

namespace uniscope::d2 {

RigGeometryReport2D rigGeometryReport2D(const Skeleton2D& skeleton) {
    RigGeometryReport2D report;
    const std::size_t count = skeleton.boneCount();
    if (count == 0) {
        return report;
    }
    float lowest = 0.0f;
    float highest = 0.0f;
    for (std::size_t bone = 0; bone < count; ++bone) {
        const int index = static_cast<int>(bone);
        const BoneTransform2D& transform = skeleton.worldTransform(index);
        lowest = std::min(lowest, transform.y);
        highest = std::max(highest, transform.y);
        float tipX = 0.0f;
        float tipY = 0.0f;
        skeleton.worldPoint(index, 1.0f, tipX, tipY);
        lowest = std::min(lowest, tipY);
        highest = std::max(highest, tipY);
    }
    report.lowestY = lowest;
    report.highestY = highest;
    report.figureHeight = highest - lowest;
    return report;
}

float rigBoneTipY2D(const Skeleton2D& skeleton, int boneIndex) {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(skeleton.boneCount())) {
        return 0.0f;
    }
    float tipX = 0.0f;
    float tipY = 0.0f;
    skeleton.worldPoint(boneIndex, 1.0f, tipX, tipY);
    return tipY;
}

}  // namespace uniscope::d2
