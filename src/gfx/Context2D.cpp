/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/Context2D.h"

#include "Renderer2D.h"

namespace uniscope::d2 {

Context2D::Context2D(Renderer2D& renderer)
    : renderer_(renderer) {}

Renderer2D& Context2D::renderer() {
    return renderer_;
}

const Renderer2D& Context2D::renderer() const {
    return renderer_;
}

void Context2D::beginFrame() {
    renderer_.beginFrame();
}

void Context2D::endFrame() {
    renderer_.endFrame();
}

void Context2D::beginRectBatch(float aspect) {
    renderer_.beginRectBatch(aspect);
}

void Context2D::flushRectBatch() {
    renderer_.flushRectBatch();
}

void Context2D::drawRect(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float aspect,
    float r,
    float g,
    float b) const {
    renderer_.drawRect(x, y, halfWidth, halfHeight, aspect, r, g, b);
}

void Context2D::drawRectAlpha(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float aspect,
    float r,
    float g,
    float b,
    float a) const {
    renderer_.drawRectAlpha(x, y, halfWidth, halfHeight, aspect, r, g, b, a);
}

} // namespace uniscope::d2
