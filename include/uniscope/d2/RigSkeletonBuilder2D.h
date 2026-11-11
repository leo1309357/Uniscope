/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RIGSKELETONBUILDER2D_H
#define UNISCOPE_D2_RIGSKELETONBUILDER2D_H

#include "Skeleton.h"

#include <cstddef>
#include <functional>

namespace uniscope::d2 {

// One bone in a rig's assembly table. Names are plain strings so the table can
// live with the character definition; the library only needs the parent link,
// the rest offset in the parent's frame, the length and the rest direction.
struct RigBoneSpec2D {
    const char* name = nullptr;
    const char* parent = nullptr;
    // Offsets in the parent's frame: +X runs down the parent's bone, +Y is
    // perpendicular. Chained bones sit at their parent's length here, and
    // left/right separation goes across - never along, because on a rotated
    // parent the along axis is the up-down axis and silently stacks the pair.
    float alongParent = 0.0f;
    float acrossParent = 0.0f;
    float length = 0.0f;
    // The direction the bone points in the rig's rest pose, in degrees. The
    // builder derives the parent-relative local rotation from it, so the table
    // reads as directions instead of numbers that each have to be checked
    // against their parent.
    float worldDegrees = 0.0f;
};

// Builds `skeleton` from a spec table.
//
// The table must be ordered parents-before-children (the draw order usually
// already is). `skip` may reject a spec - an optional chain the profile turned
// off - and any child whose parent is missing or skipped is dropped with it.
// The builder finalises the hierarchy, captures the rest pose and solves once,
// which is exactly the sequence the rig needs before any clip is authored.
bool buildRigSkeleton2D(
    Skeleton2D& skeleton,
    const RigBoneSpec2D* specs,
    std::size_t count,
    const std::function<bool(const RigBoneSpec2D&)>& skip = {});

// Rest world direction of a named bone in the spec table, in degrees. Exposed
// for tooling that wants to draw or diff the table.
float rigSpecWorldDegrees2D(const RigBoneSpec2D* specs, std::size_t count,
                            const char* name);

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_RIGSKELETONBUILDER2D_H
