/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_VISUAL_FEATURE_REGISTRY_H
#define UNISCOPE_D2_VISUAL_FEATURE_REGISTRY_H

#include "uniscope/d2/RenderPass.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace uniscope::d2 {

class Context2D;

using VisualFeaturePass = std::function<void(Context2D&, float aspect)>;

class VisualFeatureRegistry {
public:
    void registerPass(std::string name, VisualFeaturePass pass);
    void unregisterPass(const std::string& name);
    void executeAll(Context2D& context, float aspect) const;

private:
    std::unordered_map<std::string, VisualFeaturePass> passes_;
};

} // namespace uniscope::d2

#endif
