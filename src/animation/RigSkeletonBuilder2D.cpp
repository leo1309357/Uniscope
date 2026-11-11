/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RigSkeletonBuilder2D.h"

#include <cmath>
#include <string>

namespace uniscope::d2 {

namespace {

constexpr float kDeg2Rad = 3.14159265359f / 180.0f;

}  // namespace

float rigSpecWorldDegrees2D(const RigBoneSpec2D* specs, std::size_t count,
                            const char* name) {
    if (specs == nullptr || name == nullptr) {
        return 0.0f;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (specs[i].name != nullptr && std::string(specs[i].name) == name) {
            return specs[i].worldDegrees;
        }
    }
    return 0.0f;
}

bool buildRigSkeleton2D(
    Skeleton2D& skeleton,
    const RigBoneSpec2D* specs,
    std::size_t count,
    const std::function<bool(const RigBoneSpec2D&)>& skip) {
    skeleton = Skeleton2D{};
    if (specs == nullptr) {
        return false;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const RigBoneSpec2D& spec = specs[i];
        if (spec.name == nullptr || (skip && skip(spec))) {
            continue;
        }
        Bone2D bone;
        bone.name = spec.name;
        bone.parent = spec.parent != nullptr ? skeleton.findBone(spec.parent) : -1;
        // A child of a bone that was skipped (or never existed) goes with it:
        // assembling it against a missing parent would silently detach it.
        if (spec.parent != nullptr && bone.parent < 0) {
            continue;
        }
        bone.localX = spec.alongParent;
        bone.localY = spec.acrossParent;
        const float parentWorld = spec.parent != nullptr
            ? rigSpecWorldDegrees2D(specs, count, spec.parent)
            : 0.0f;
        bone.rotation = (spec.worldDegrees - parentWorld) * kDeg2Rad;
        bone.length = spec.length;
        skeleton.addBone(bone);
    }

    skeleton.finalize();
    skeleton.captureRestPose();
    skeleton.solve();
    return skeleton.valid();
}

}  // namespace uniscope::d2
