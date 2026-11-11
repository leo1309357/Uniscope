/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "ShaderLibrary.h"

#include "ShaderAssets.h"

#include "uniscope/d2/AssetPaths.h"

namespace uniscope::d2 {

ShaderProgram& ShaderLibrary::program(ShaderId id) {
    const auto existing = programs_.find(id);
    if (existing != programs_.end()) {
        return *existing->second;
    }

    std::unique_ptr<ShaderProgram> loaded = loadProgram(id);
    ShaderProgram& reference = *loaded;
    programs_.emplace(id, std::move(loaded));
    return reference;
}

bool ShaderLibrary::has(ShaderId id) const {
    return programs_.find(id) != programs_.end();
}

void ShaderLibrary::clear() {
    programs_.clear();
}

std::unique_ptr<ShaderProgram> ShaderLibrary::loadProgram(ShaderId id) {
    const ShaderAssetPaths paths = shaderAssetPaths(id);
    return std::make_unique<ShaderProgram>(
        uniscope::d2::AssetPaths::resolve(paths.vertex),
        uniscope::d2::AssetPaths::resolve(paths.fragment));
}

} // namespace uniscope::d2
