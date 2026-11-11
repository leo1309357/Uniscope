/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/VisualFeatureRegistry.h"

#include "uniscope/d2/Context2D.h"

namespace uniscope::d2 {

void VisualFeatureRegistry::registerPass(std::string name, VisualFeaturePass pass) {
    passes_[std::move(name)] = std::move(pass);
}

void VisualFeatureRegistry::unregisterPass(const std::string& name) {
    passes_.erase(name);
}

void VisualFeatureRegistry::executeAll(Context2D& context, float aspect) const {
    for (const auto& entry : passes_) {
        if (entry.second) {
            entry.second(context, aspect);
        }
    }
}

} // namespace uniscope::d2
