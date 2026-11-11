/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/ShadowCaster.h"

namespace uniscope::d2 {

void ShadowCaster2D::clear() {
    polygons_.clear();
}

void ShadowCaster2D::addConvexPolygon(std::vector<Vec2> vertices, float alpha) {
    if (vertices.size() < 3) {
        return;
    }
    // A plain vertex is a fully-opaque one; the game path only supplies weights
    // when it wants a falloff (see addConvexPolygonWeighted).
    ShadowPolygon2D polygon;
    polygon.alpha = alpha;
    polygon.vertices.reserve(vertices.size());
    for (const Vec2& v : vertices) {
        polygon.vertices.push_back({v.x, v.y, 1.0f});
    }
    polygons_.push_back(std::move(polygon));
}

void ShadowCaster2D::addConvexPolygonWeighted(
    std::vector<ShadowVertex2D> vertices,
    float alpha) {
    if (vertices.size() < 3) {
        return;
    }
    polygons_.push_back(ShadowPolygon2D{std::move(vertices), alpha});
}

} // namespace uniscope::d2
