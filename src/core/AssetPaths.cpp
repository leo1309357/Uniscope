/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/AssetPaths.h"

#include <filesystem>

namespace uniscope::d2 {

namespace {

std::string gRoot;

std::filesystem::path toPath(const std::string& relativePath) {
    if (gRoot.empty()) {
        return std::filesystem::u8path(relativePath);
    }
    return std::filesystem::u8path(gRoot) / std::filesystem::u8path(relativePath);
}

} // namespace

void AssetPaths::setRoot(std::string root) {
    gRoot = std::move(root);
    if (!gRoot.empty() && (gRoot.back() == '/' || gRoot.back() == '\\')) {
        gRoot.pop_back();
    }
}

const std::string& AssetPaths::root() {
    return gRoot;
}

std::string AssetPaths::resolve(const std::string& relativePath) {
    return toPath(relativePath).u8string();
}

bool AssetPaths::exists(const std::string& relativePath) {
    std::error_code error;
    return std::filesystem::exists(toPath(relativePath), error);
}

} // namespace uniscope::d2
