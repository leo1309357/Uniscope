/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_SKELETON_H
#define UNISCOPE_D2_SKELETON_H

#include <cstdint>
#include <string>
#include <vector>

namespace uniscope::d2 {

// A single joint in a 2D hierarchy.
//
// Local values are expressed in the parent's frame: the joint origin sits at
// (localX, localY) and the bone points along its local +X axis rotated by
// `rotation` (radians, counter-clockwise). `length` is the anatomical length of
// the segment and is what skin quads are scaled against, so a rig stays
// resolution independent.
struct Bone2D {
    std::string name;
    int parent = -1;
    float localX = 0.0f;
    float localY = 0.0f;
    float rotation = 0.0f;
    float length = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// World-space result of one forward-kinematics pass.
struct BoneTransform2D {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    float cosAngle = 1.0f;
    float sinAngle = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// The neutral local transform of one bone, captured when the rig is bound.
struct BoneBind2D {
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// A named hierarchy of bones.
//
// The rest pose is captured once and reused as the neutral target for every
// animation clip, so clips only need to author deltas for the joints they
// actually move.
//
// Art is attached by bone index rather than through a second name table: a rig
// binds its skin with `RigSkinSlot2D::boneIndex`, so the skeleton used to also
// carry a parallel list of mesh-slot names that nothing ever read. Two tables for
// one relationship is two things to keep in step, and the one that was not being
// kept in step was the one nobody consulted.
class Skeleton2D {
public:
    void addBone(Bone2D bone);

    // Sorts bones into parent-before-child order and validates the hierarchy.
    // Safe to call repeatedly; a no-op once the order is already valid.
    bool finalize();

    std::size_t boneCount() const { return bones_.size(); }
    const Bone2D& bone(std::size_t index) const { return bones_[index]; }
    const std::vector<Bone2D>& bones() const { return bones_; }

    int findBone(const std::string& name) const;
    bool valid() const { return valid_; }

    // Binds the current pose as the neutral pose for later resetToRestPose().
    void captureRestPose();
    void resetToRestPose();
    const std::vector<BoneBind2D>& bindPose() const { return bind_; }
    const BoneBind2D& bindPose(int boneIndex) const;

    // Forward kinematics over the current local transforms.
    void solve();
    const BoneTransform2D& worldTransform(int boneIndex) const;

    // Point along a bone in world space: `along` 0 = joint, 1 = bone tip.
    void worldPoint(int boneIndex, float along, float& outX, float& outY) const;

    void setLocalTransform(int boneIndex, float x, float y, float rotation);
    void setLocalScale(int boneIndex, float scaleX, float scaleY);
    void localTransform(int boneIndex, float& outX, float& outY, float& outRotation) const;

private:
    std::vector<Bone2D> bones_;
    std::vector<BoneTransform2D> world_;
    std::vector<BoneBind2D> bind_;
    bool finalized_ = false;
    bool valid_ = false;
    // Whether `bind_` has been captured. Internal: nothing outside needs to ask,
    // since asking is only ever a way to be told "resetToRestPose will do the right
    // thing", which it does either way.
    bool hasRestPose_ = false;
};

} // namespace uniscope::d2

#endif
