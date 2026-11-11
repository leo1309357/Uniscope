/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "FontPlatformPaths.h"

#include <filesystem>

namespace uniscope::d2 {

bool absolutePathExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    return std::filesystem::exists(path, error) && std::filesystem::is_regular_file(path, error);
}

void appendExistingAbsolutePaths(
    std::vector<std::string>& paths,
    const char* const* candidates,
    std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        const char* candidate = candidates[index];
        if (candidate == nullptr || candidate[0] == '\0') {
            continue;
        }
        if (absolutePathExists(candidate)) {
            paths.emplace_back(candidate);
        }
    }
}

void appendPlatformDefaultSystemFonts(std::vector<std::string>& paths) {
#if defined(_WIN32)
    static const char* const kWindowsFonts[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/segoeui.ttc",
        "C:/Windows/Fonts/arial.ttf",
    };
    appendExistingAbsolutePaths(paths, kWindowsFonts, sizeof(kWindowsFonts) / sizeof(kWindowsFonts[0]));
#elif defined(__APPLE__)
    static const char* const kMacOSFonts[] = {
        "/System/Library/Fonts/SFNSText.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    appendExistingAbsolutePaths(paths, kMacOSFonts, sizeof(kMacOSFonts) / sizeof(kMacOSFonts[0]));
#elif defined(__linux__) && !defined(__ANDROID__)
    static const char* const kLinuxFonts[] = {
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/local/share/fonts/NotoSans-Regular.ttf",
    };
    appendExistingAbsolutePaths(paths, kLinuxFonts, sizeof(kLinuxFonts) / sizeof(kLinuxFonts[0]));
#elif defined(__ANDROID__)
    // AOSP always ships DroidSans/Roboto; NotoCJK covers CJK when bundled.
    static const char* const kAndroidFonts[] = {
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/DroidSans.ttf",
        "/system/fonts/NotoSans-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
    };
    appendExistingAbsolutePaths(paths, kAndroidFonts, sizeof(kAndroidFonts) / sizeof(kAndroidFonts[0]));
#endif
}

} // namespace uniscope::d2
