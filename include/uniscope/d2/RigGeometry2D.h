/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RIGGEOMETRY2D_H
#define UNISCOPE_D2_RIGGEOMETRY2D_H

#include "Skeleton.h"

namespace uniscope::d2 {

// The rig-space extents of a solved skeleton, measured on bone origins and
// tips. Renderers use it to place contact shadows, gameplay to derive a body
// box; neither has to walk the skeleton itself.
struct RigGeometryReport2D {
    float lowestY = 0.0f;
    float highestY = 0.0f;
    float figureHeight = 0.0f;
};

// Union of every bone origin and tip. Returns an empty report for a skeleton
// with no bones.
RigGeometryReport2D rigGeometryReport2D(const Skeleton2D& skeleton);

// World Y of a bone's tip. Returns 0 for an invalid index, which keeps callers
// from branching on a bone the rig may not have (an optional chain).
float rigBoneTipY2D(const Skeleton2D& skeleton, int boneIndex);

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_RIGGEOMETRY2D_H
