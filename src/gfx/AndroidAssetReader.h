/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_ANDROID_ASSET_READER_H
#define UNISCOPE_D2_GFX_ANDROID_ASSET_READER_H

#include <string>
#include <vector>

namespace uniscope::d2 {

// APK asset access for Android. The game deploys shaders/resources/data into
// the Gradle assets/ tree, so at runtime they live inside the APK and are not
// visible through std::filesystem. Desktop builds ignore this entirely.
//
// The host (Eraphanis Application / NativeActivity glue) registers the
// native AAssetManager once during android_main; file loaders consult it only
// as a fallback after the regular filesystem attempt, so desktop behaviour is
// unchanged.
void SetAndroidAssetManager(void* assetManager);
void* AndroidAssetManager();

// Extracts the APK-relative asset path from an absolute or relative file
// path, e.g. ".../shaders/rect2d.vert" -> "shaders/rect2d.vert".
// Returns empty when no known anchor is found.
std::string ToAndroidAssetPath(const std::string& filePath);

bool AndroidAssetExists(const std::string& filePath);
std::string AndroidAssetReadText(const std::string& filePath);
std::vector<unsigned char> AndroidAssetReadBytes(const std::string& filePath);

} // namespace uniscope::d2

#endif
