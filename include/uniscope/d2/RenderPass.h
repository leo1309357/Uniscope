/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RENDER_PASS_H
#define UNISCOPE_D2_RENDER_PASS_H

#include "uniscope/d2/RenderTarget.h"

#include <functional>

namespace uniscope::d2 {

class Context2D;

struct RenderPassStats {
    int drawCalls = 0;
};

class RenderPass {
public:
    using DrawFn = std::function<void(Context2D&)>;

    void executeToScreen(Context2D& context, const DrawFn& draw);
    void executeToTarget(RenderTarget& target, Context2D& context, const DrawFn& draw);

    const RenderPassStats& stats() const { return stats_; }

private:
    RenderPassStats stats_;
};

} // namespace uniscope::d2

#endif
