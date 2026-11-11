/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RIGATTACHMENT2D_H
#define UNISCOPE_D2_RIGATTACHMENT2D_H

#include "Skeleton.h"

#include <string>

namespace uniscope::d2 {

// A point where something hangs off a rig: a bone, an offset in that bone's
// local space (x along the bone, y across it), a rotation and a scale, plus
// the frame name an atlas or sprite batch resolves the art by.
//
// This is the mechanism only. What is hung (a sword, a lantern, an accessory)
// and its art are content and stay with the game.
struct RigAttachment2D {
    int bone = -1;
    float localX = 0.0f;
    float localY = 0.0f;
    float rotation = 0.0f;
    float scale = 1.0f;
    // Carried, not interpreted: the art frame a renderer should draw here.
    std::string frame;

    bool valid() const { return bone >= 0; }

    // Where the attachment lands in rig space for this pose.
    struct Placement2D {
        float x = 0.0f;
        float y = 0.0f;
        // Bone world rotation plus the attachment's own offset.
        float rotation = 0.0f;
        float scale = 1.0f;
        bool valid = false;
    };
    Placement2D resolve(const Skeleton2D& posed) const;
};

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_RIGATTACHMENT2D_H
