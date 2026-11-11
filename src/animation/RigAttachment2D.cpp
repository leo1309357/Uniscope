/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RigAttachment2D.h"

namespace uniscope::d2 {

RigAttachment2D::Placement2D RigAttachment2D::resolve(const Skeleton2D& posed) const {
    Placement2D placement;
    if (bone < 0 || bone >= static_cast<int>(posed.boneCount())) {
        return placement;
    }
    const BoneTransform2D& transform = posed.worldTransform(bone);
    // A non-positive scale is a caller error; treat it as 1 rather than
    // collapsing the attachment to a point or mirroring it.
    const float resolvedScale = scale > 0.0f ? scale : 1.0f;
    const float along = localX * resolvedScale;
    const float across = localY * resolvedScale;
    placement.x = transform.x + along * transform.cosAngle - across * transform.sinAngle;
    placement.y = transform.y + along * transform.sinAngle + across * transform.cosAngle;
    placement.rotation = transform.rotation + rotation;
    placement.scale = resolvedScale;
    placement.valid = true;
    return placement;
}

}  // namespace uniscope::d2
