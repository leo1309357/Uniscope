/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_SCENE_OBJECT2D_H
#define UNISCOPE_D2_SCENE_OBJECT2D_H

#include <cstdint>

namespace uniscope::d2 {

// Base class (GameObject) for every 2D scene entity: visible world objects,
// lights, occluders and shadow casters alike.
//
// Only transform + visibility + identity live here. Rendering, lighting and
// shadowing are separate interfaces so a subclass picks exactly the roles it
// plays (see LightSource2D.h, SurfaceObject2D.h). Header-only, no renderer
// dependency, safe to include from game code and tests.
struct Transform2D {
    float x = 0.0f;
    float y = 0.0f;
    float halfWidth = 0.5f;
    float halfHeight = 0.5f;
    float rotation = 0.0f; // radians, counter-clockwise
    float layer = 0.0f;    // painter stack offset inside one tile
};

struct Bounds2D {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

class SceneObject2D {
public:
    SceneObject2D() = default;
    explicit SceneObject2D(Transform2D transform, bool visible = true)
        : transform_(transform), visible_(visible), id_(nextId()) {}
    virtual ~SceneObject2D() = default;

    // Copyable: a copy is a distinct object with a fresh id.
    SceneObject2D(const SceneObject2D& other)
        : transform_(other.transform_), visible_(other.visible_), id_(nextId()) {}
    SceneObject2D& operator=(const SceneObject2D& other) {
        transform_ = other.transform_;
        visible_ = other.visible_;
        return *this;
    }
    SceneObject2D(SceneObject2D&&) noexcept = default;
    SceneObject2D& operator=(SceneObject2D&&) noexcept = default;

    std::uint32_t id() const { return id_; }
    const Transform2D& transform() const { return transform_; }
    void setTransform(Transform2D transform) { transform_ = transform; }
    void setPosition(float x, float y) {
        transform_.x = x;
        transform_.y = y;
    }

    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }

    Bounds2D bounds() const {
        return {
            transform_.x - transform_.halfWidth,
            transform_.x + transform_.halfWidth,
            transform_.y - transform_.halfHeight,
            transform_.y + transform_.halfHeight};
    }

    bool overlaps(const Bounds2D& other) const {
        const Bounds2D self = bounds();
        return self.minX <= other.maxX && self.maxX >= other.minX
            && self.minY <= other.maxY && self.maxY >= other.minY;
    }

    // Painter key: back-to-front, descending. Matches the game depthSortKey
    // convention (larger worldY draws first, layer lifts within a tile).
    float sortKey() const { return transform_.y - transform_.layer * 0.03f; }

    virtual void update(float /*deltaSeconds*/) {}

protected:
    Transform2D transform_{};
    bool visible_ = true;

private:
    static std::uint32_t nextId() {
        static std::uint32_t counter = 1;
        return counter++;
    }
    std::uint32_t id_ = nextId();
};

// Backwards-friendly alias: game code may refer to GameObject2D.
using GameObject2D = SceneObject2D;

} // namespace uniscope::d2

#endif
