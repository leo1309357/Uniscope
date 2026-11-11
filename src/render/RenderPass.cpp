/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RenderPass.h"

#include "uniscope/d2/Context2D.h"

#include "Renderer2D.h"

namespace uniscope::d2 {

void RenderPass::executeToScreen(Context2D& context, const DrawFn& draw) {
    // Count the GL draws the pass actually pays for (one per batch flush)
    // instead of claiming one per DrawFn - a batched pass and an empty pass
    // both used to report drawCalls == 1.
    const std::uint64_t flushesBefore =
        context.renderer().rectBatchFlushes() + context.renderer().sdfBatchFlushes();
    context.beginFrame();
    if (draw) {
        draw(context);
    }
    context.endFrame();
    stats_.drawCalls = static_cast<int>(
        context.renderer().rectBatchFlushes() + context.renderer().sdfBatchFlushes() - flushesBefore);
}

void RenderPass::executeToTarget(RenderTarget& target, Context2D& context, const DrawFn& draw) {
    stats_.drawCalls = 0;
    if (!target.valid()) {
        return;
    }
    // bind() takes over the framebuffer AND the viewport without saving either,
    // so snapshot the incoming viewport here: unbind() restores it by size, and
    // the old hardcoded unbind(1, 1) left every later draw rendering into a
    // single pixel.
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    const std::uint64_t flushesBefore =
        context.renderer().rectBatchFlushes() + context.renderer().sdfBatchFlushes();
    target.bind();
    context.beginFrame();
    if (draw) {
        draw(context);
    }
    context.endFrame();
    target.unbind(previousViewport[2], previousViewport[3]);
    stats_.drawCalls = static_cast<int>(
        context.renderer().rectBatchFlushes() + context.renderer().sdfBatchFlushes() - flushesBefore);
}

} // namespace uniscope::d2
