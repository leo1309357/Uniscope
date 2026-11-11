/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_CONTEXT2D_H
#define UNISCOPE_D2_CONTEXT2D_H

namespace uniscope::d2 {

class Renderer2D;

class Context2D {
public:
    explicit Context2D(Renderer2D& renderer);

    Renderer2D& renderer();
    const Renderer2D& renderer() const;

    void beginFrame();
    void endFrame();

    void beginRectBatch(float aspect);
    void flushRectBatch();

    void drawRect(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float aspect,
        float r,
        float g,
        float b) const;

    void drawRectAlpha(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float aspect,
        float r,
        float g,
        float b,
        float a) const;

private:
    Renderer2D& renderer_;
};

} // namespace uniscope::d2

#endif
