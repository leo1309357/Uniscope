/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "AndroidAssetReader.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

namespace uniscope::d2 {

namespace {
void* gAssetManager = nullptr;

const char* kAnchors[] = {
    "shaders/",
    "resources/",
    "data/",
    "assets/",
    "fonts/",
};
} // namespace

void SetAndroidAssetManager(void* assetManager) {
    gAssetManager = assetManager;
}

void* AndroidAssetManager() {
    return gAssetManager;
}

std::string ToAndroidAssetPath(const std::string& filePath) {
    if (filePath.empty()) {
        return {};
    }
    std::string normalized = filePath;
    for (char& ch : normalized) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    for (const char* anchor : kAnchors) {
        const std::string key(anchor);
        const std::size_t pos = normalized.rfind(key);
        if (pos != std::string::npos) {
            // "assets/..." anchor itself is the APK root: strip the prefix.
            if (key == std::string("assets/")) {
                return normalized.substr(pos + key.size());
            }
            return normalized.substr(pos);
        }
    }
    return {};
}

bool AndroidAssetExists(const std::string& filePath) {
#if defined(__ANDROID__)
    if (gAssetManager == nullptr) {
        return false;
    }
    const std::string assetPath = ToAndroidAssetPath(filePath);
    if (assetPath.empty()) {
        return false;
    }
    AAssetManager* manager = static_cast<AAssetManager*>(gAssetManager);
    AAsset* asset = AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_STREAMING);
    if (asset != nullptr) {
        AAsset_close(asset);
        return true;
    }
    return false;
#else
    (void)filePath;
    return false;
#endif
}

std::string AndroidAssetReadText(const std::string& filePath) {
#if defined(__ANDROID__)
    if (gAssetManager == nullptr) {
        return {};
    }
    const std::string assetPath = ToAndroidAssetPath(filePath);
    if (assetPath.empty()) {
        return {};
    }
    AAssetManager* manager = static_cast<AAssetManager*>(gAssetManager);
    AAsset* asset = AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        return {};
    }
    const std::size_t length = static_cast<std::size_t>(AAsset_getLength(asset));
    std::string content(length, '\0');
    if (length > 0) {
        AAsset_read(asset, content.data(), length);
    }
    AAsset_close(asset);
    return content;
#else
    (void)filePath;
    return {};
#endif
}

std::vector<unsigned char> AndroidAssetReadBytes(const std::string& filePath) {
#if defined(__ANDROID__)
    if (gAssetManager == nullptr) {
        return {};
    }
    const std::string assetPath = ToAndroidAssetPath(filePath);
    if (assetPath.empty()) {
        return {};
    }
    AAssetManager* manager = static_cast<AAssetManager*>(gAssetManager);
    AAsset* asset = AAssetManager_open(manager, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        return {};
    }
    const std::size_t length = static_cast<std::size_t>(AAsset_getLength(asset));
    std::vector<unsigned char> bytes(length);
    if (length > 0) {
        AAsset_read(asset, bytes.data(), length);
    }
    AAsset_close(asset);
    return bytes;
#else
    (void)filePath;
    return {};
#endif
}

} // namespace uniscope::d2
