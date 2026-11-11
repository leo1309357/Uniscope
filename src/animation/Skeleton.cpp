/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/Skeleton.h"

#include <algorithm>
#include <cmath>

namespace uniscope::d2 {

namespace {

float wrapAngle(float radians) {
    // Canonical +-pi form: coterminal with the input for every trigonometric
    // consumer, and consistent with shortestAngleDelta's range.
    constexpr float kPi = 3.14159265359f;
    constexpr float kTwoPi = 6.28318530718f;
    while (radians > kPi) {
        radians -= kTwoPi;
    }
    while (radians < -kPi) {
        radians += kTwoPi;
    }
    return radians;
}

} // namespace

void Skeleton2D::addBone(Bone2D bone) {
    bones_.push_back(std::move(bone));
    finalized_ = false;
}

int Skeleton2D::findBone(const std::string& name) const {
    for (std::size_t index = 0; index < bones_.size(); ++index) {
        if (bones_[index].name == name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool Skeleton2D::finalize() {
    const int boneCount = static_cast<int>(bones_.size());
    bool ordered = true;
    for (int index = 0; index < boneCount; ++index) {
        const int parent = bones_[static_cast<std::size_t>(index)].parent;
        if (parent < 0) {
            continue;
        }
        if (parent >= boneCount || parent == index) {
            valid_ = false;
            return false;
        }
        if (parent > index) {
            ordered = false;
        }
    }

    if (!ordered) {
        // Depth-first reorder so every parent is emitted before its children.
        std::vector<int> order;
        order.reserve(bones_.size());
        std::vector<int> depth(bones_.size(), 0);
        std::vector<int> remaining(bones_.size());
        for (std::size_t index = 0; index < bones_.size(); ++index) {
            remaining[index] = static_cast<int>(index);
        }

        // Insertion sort by hierarchy depth; ties keep their original order so
        // authored draw orders stay stable.
        const auto depthOf = [&](int index) {
            int value = 0;
            int guard = 0;
            int cursor = index;
            while (cursor >= 0 && guard++ <= boneCount) {
                cursor = bones_[static_cast<std::size_t>(cursor)].parent;
                ++value;
            }
            return value;
        };
        for (std::size_t index = 0; index < bones_.size(); ++index) {
            depth[index] = depthOf(static_cast<int>(index));
        }
        std::stable_sort(
            remaining.begin(),
            remaining.end(),
            [&](int left, int right) { return depth[static_cast<std::size_t>(left)] < depth[static_cast<std::size_t>(right)]; });

        std::vector<Bone2D> sorted;
        std::vector<int> remap(bones_.size(), -1);
        sorted.reserve(bones_.size());
        for (std::size_t newIndex = 0; newIndex < remaining.size(); ++newIndex) {
            const int oldIndex = remaining[newIndex];
            remap[static_cast<std::size_t>(oldIndex)] = static_cast<int>(newIndex);
            sorted.push_back(bones_[static_cast<std::size_t>(oldIndex)]);
        }
        for (Bone2D& moved : sorted) {
            if (moved.parent >= 0) {
                moved.parent = remap[static_cast<std::size_t>(moved.parent)];
            }
        }
        bones_.swap(sorted);
    }

    valid_ = true;
    finalized_ = true;
    world_.assign(bones_.size(), BoneTransform2D{});
    if (hasRestPose_) {
        // Bone indices may have changed while reordering; rebuild the bind
        // table from the reordered hierarchy so resetToRestPose stays correct.
        bind_.clear();
        bind_.reserve(bones_.size());
        for (const Bone2D& entry : bones_) {
            bind_.push_back(BoneBind2D{entry.localX, entry.localY, entry.rotation, entry.scaleX, entry.scaleY});
        }
    }
    return true;
}

void Skeleton2D::captureRestPose() {
    hasRestPose_ = true;
    bind_.clear();
    bind_.reserve(bones_.size());
    for (const Bone2D& entry : bones_) {
        bind_.push_back(BoneBind2D{entry.localX, entry.localY, entry.rotation, entry.scaleX, entry.scaleY});
    }
}

const BoneBind2D& Skeleton2D::bindPose(int boneIndex) const {
    static const BoneBind2D kIdentity{};
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bind_.size())) {
        return kIdentity;
    }
    return bind_[static_cast<std::size_t>(boneIndex)];
}

void Skeleton2D::resetToRestPose() {
    if (!hasRestPose_) {
        captureRestPose();
    }
    for (std::size_t index = 0; index < bones_.size() && index < bind_.size(); ++index) {
        const BoneBind2D& pose = bind_[index];
        bones_[index].localX = pose.x;
        bones_[index].localY = pose.y;
        bones_[index].rotation = pose.rotation;
        bones_[index].scaleX = pose.scaleX;
        bones_[index].scaleY = pose.scaleY;
    }
}

void Skeleton2D::solve() {
    if (world_.size() != bones_.size()) {
        world_.assign(bones_.size(), BoneTransform2D{});
    }
    if (!finalized_) {
        finalize();
    }

    for (std::size_t index = 0; index < bones_.size(); ++index) {
        const Bone2D& entry = bones_[index];
        BoneTransform2D& out = world_[index];
        const float localRotation = entry.rotation;
        const float localCos = std::cos(localRotation);
        const float localSin = std::sin(localRotation);

        if (entry.parent < 0) {
            out.x = entry.localX;
            out.y = entry.localY;
            out.rotation = wrapAngle(localRotation);
            out.cosAngle = localCos;
            out.sinAngle = localSin;
            out.scaleX = entry.scaleX;
            out.scaleY = entry.scaleY;
            continue;
        }

        const BoneTransform2D& parent = world_[static_cast<std::size_t>(entry.parent)];
        out.scaleX = parent.scaleX * entry.scaleX;
        out.scaleY = parent.scaleY * entry.scaleY;
        // Scale first, then rotate.
        //
        // Scaling each output axis after rotating is a different map from scaling
        // the offset in the parent's own frame, and the two agree only while the two
        // scales are equal. With an anisotropic parent the rotation mixes x into y,
        // so multiplying the rotated x by scaleX and the rotated y by scaleY applies
        // the scale on the wrong axis: the child drifts off its parent as soon as the
        // parent is both rotated and stretched. Every clip authored so far scales
        // uniformly, which is why it went unnoticed - but squash-and-stretch is
        // exactly the thing that wants a non-uniform scale, and it would be wrong
        // there.
        const float scaledX = entry.localX * parent.scaleX;
        const float scaledY = entry.localY * parent.scaleY;
        out.x = parent.x + scaledX * parent.cosAngle - scaledY * parent.sinAngle;
        out.y = parent.y + scaledX * parent.sinAngle + scaledY * parent.cosAngle;
        const float worldRotation = wrapAngle(parent.rotation + localRotation);
        out.rotation = worldRotation;
        out.cosAngle = std::cos(worldRotation);
        out.sinAngle = std::sin(worldRotation);
    }
}

const BoneTransform2D& Skeleton2D::worldTransform(int boneIndex) const {
    static const BoneTransform2D kIdentity{};
    if (boneIndex < 0 || boneIndex >= static_cast<int>(world_.size())) {
        return kIdentity;
    }
    return world_[static_cast<std::size_t>(boneIndex)];
}

void Skeleton2D::worldPoint(int boneIndex, float along, float& outX, float& outY) const {
    const BoneTransform2D& transform = worldTransform(boneIndex);
    const Bone2D& entry = bone(static_cast<std::size_t>(std::max(0, boneIndex)));
    // Honor the bone's animated scale so a scaled limb's tip lands where its
    // child bone is positioned. Without this, worldPoint(shin, 1) overshoots
    // the ankle whenever the shin is shortened (e.g. a head-on knee fold),
    // and the foot quad drifts off the trouser cuff. entry.scaleX is the
    // posed local scale; child origins already multiply by it in solve().
    const float reach = along * entry.length * entry.scaleX;
    outX = transform.x + reach * transform.cosAngle;
    outY = transform.y + reach * transform.sinAngle;
}

void Skeleton2D::setLocalTransform(int boneIndex, float x, float y, float rotation) {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) {
        return;
    }
    Bone2D& entry = bones_[static_cast<std::size_t>(boneIndex)];
    entry.localX = x;
    entry.localY = y;
    entry.rotation = rotation;
}

void Skeleton2D::setLocalScale(int boneIndex, float scaleX, float scaleY) {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) {
        return;
    }
    Bone2D& entry = bones_[static_cast<std::size_t>(boneIndex)];
    entry.scaleX = scaleX;
    entry.scaleY = scaleY;
}

void Skeleton2D::localTransform(int boneIndex, float& outX, float& outY, float& outRotation) const {
    if (boneIndex < 0 || boneIndex >= static_cast<int>(bones_.size())) {
        outX = 0.0f;
        outY = 0.0f;
        outRotation = 0.0f;
        return;
    }
    const Bone2D& entry = bones_[static_cast<std::size_t>(boneIndex)];
    outX = entry.localX;
    outY = entry.localY;
    outRotation = entry.rotation;
}

} // namespace uniscope::d2
