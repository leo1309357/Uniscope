/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "FontAssets.h"

#include "uniscope/d2/AssetPaths.h"
#include "AndroidAssetReader.h"
#include "FontPlatformPaths.h"

#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace uniscope::d2 {

namespace {

bool appendIfExists(std::vector<std::string>& paths, const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return false;
    }

    const std::string normalized = path.u8string();
    if (std::find(paths.begin(), paths.end(), normalized) == paths.end()) {
        paths.push_back(normalized);
    }
    return true;
}

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= buffer.size()) {
        return {};
    }
    return std::filesystem::path(buffer.data()).parent_path();
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#elif defined(__ANDROID__)
    return {};
#elif defined(__linux__)
    std::error_code error;
    const std::filesystem::path exePath = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) {
        return {};
    }
    return exePath.parent_path();
#else
    return {};
#endif
}

// The directories a relative asset path is resolved against: the executable's
// own directory and its ancestors (a build tree nests the binary several levels
// below the source root), then the working directory and its parents (a manual
// run from the source tree). Both walks are bounded, so a path that exists
// nowhere cannot turn into an unbounded search.
std::vector<std::filesystem::path> searchRoots() {
    std::vector<std::filesystem::path> roots;

    std::filesystem::path exeDir = executableDirectory();
    for (int depth = 0; depth < 6 && !exeDir.empty(); ++depth) {
        roots.push_back(exeDir);
        exeDir = exeDir.parent_path();
    }

    std::error_code currentPathError;
    std::filesystem::path cursor = std::filesystem::current_path(currentPathError);
    if (!currentPathError) {
        for (int depth = 0; depth < 8 && !cursor.empty(); ++depth) {
            roots.push_back(cursor);
            if (!cursor.has_parent_path()) {
                break;
            }
            cursor = cursor.parent_path();
        }
    }
    return roots;
}

void appendResourceFontPath(std::vector<std::string>& paths, const char* relativePath) {
    if (uniscope::d2::AssetPaths::exists(relativePath)) {
        appendIfExists(paths, uniscope::d2::AssetPaths::resolve(relativePath));
    }

#if defined(__ANDROID__)
    // Packaged fonts live inside the APK, so std::filesystem never sees them
    // and every existence-filtered probe above comes up empty. Offer the
    // resources/ asset path directly; readFileBytes fetches it through
    // AAssetManager (AndroidAssetReadBytes).
    const std::string assetPath = std::string("resources/") + relativePath;
    if (AndroidAssetExists(assetPath)
        && std::find(paths.begin(), paths.end(), assetPath) == paths.end()) {
        paths.push_back(assetPath);
    }
#endif

    for (const std::filesystem::path& root : searchRoots()) {
        appendIfExists(paths, root / "resources" / relativePath);
    }
}

} // namespace

FontFamily FontAssets::primaryUiFamily() {
    return FontFamily::Default;
}

const std::vector<FontVariant>& FontAssets::defaultVariants() {
    static const std::vector<FontVariant> kVariants = {
        {"Noto Sans", "assets/eraphanis/fonts/Default/NotoSans-Regular.ttf"},
#if defined(_WIN32)
        {"Segoe UI", "assets/eraphanis/fonts/Default/segoeui.ttf"},
#endif
    };
    return kVariants;
}

const std::vector<FontVariant>& FontAssets::sourceHanSansVariants() {
    static const std::vector<FontVariant> kVariants = {
        {"SC", "assets/eraphanis/fonts/SourceHanSans/SourceHanSansSC-VF.ttf"},
        {"CN-Subset", "assets/eraphanis/fonts/SourceHanSans/Subset/SourceHanSansCN-VF.ttf"},
        {"Sans", "assets/eraphanis/fonts/SourceHanSans/SourceHanSans-VF.ttf"},
        {"TC", "assets/eraphanis/fonts/SourceHanSans/SourceHanSansTC-VF.ttf"},
        {"HC", "assets/eraphanis/fonts/SourceHanSans/SourceHanSansHC-VF.ttf"},
        {"K", "assets/eraphanis/fonts/SourceHanSans/SourceHanSansK-VF.ttf"},
        {"TW-Subset", "assets/eraphanis/fonts/SourceHanSans/Subset/SourceHanSansTW-VF.ttf"},
        {"HK-Subset", "assets/eraphanis/fonts/SourceHanSans/Subset/SourceHanSansHK-VF.ttf"},
        {"JP-Subset", "assets/eraphanis/fonts/SourceHanSans/Subset/SourceHanSansJP-VF.ttf"},
        {"KR-Subset", "assets/eraphanis/fonts/SourceHanSans/Subset/SourceHanSansKR-VF.ttf"},
        {"HW-SC", "assets/eraphanis/fonts/SourceHanSans/HW/SourceHanSansHWSC-VF.ttf"},
        {"HW-TC", "assets/eraphanis/fonts/SourceHanSans/HW/SourceHanSansHWTC-VF.ttf"},
        {"HW-HC", "assets/eraphanis/fonts/SourceHanSans/HW/SourceHanSansHWHC-VF.ttf"},
        {"HW-K", "assets/eraphanis/fonts/SourceHanSans/HW/SourceHanSansHWK-VF.ttf"},
        {"HW", "assets/eraphanis/fonts/SourceHanSans/HW/SourceHanSansHW-VF.ttf"},
    };
    return kVariants;
}

const std::vector<FontVariant>& FontAssets::variants(FontFamily family) {
    switch (family) {
    case FontFamily::Default:
        return defaultVariants();
    case FontFamily::SourceHanSans:
        return sourceHanSansVariants();
    }
    return defaultVariants();
}

std::vector<std::string> FontAssets::candidatePaths(FontFamily family) {
    std::vector<std::string> paths;
    for (const FontVariant& variant : variants(family)) {
        appendResourceFontPath(paths, variant.relativePath);
    }

    if (family == FontFamily::Default) {
        appendPlatformDefaultSystemFonts(paths);
    }

    return paths;
}

std::vector<std::string> FontAssets::treeCandidates(const std::string& relativePath) {
    std::vector<std::string> paths;
    if (relativePath.empty()) {
        return paths;
    }

    const std::filesystem::path relative = std::filesystem::u8path(relativePath);
    for (const std::filesystem::path& root : searchRoots()) {
        appendIfExists(paths, root / relative);
    }

#if defined(__ANDROID__)
    // A path under an APK asset anchor ("fonts/...", "resources/...") is read
    // through AAssetManager, so offer the asset spelling when it exists.
    const std::string assetPath = ToAndroidAssetPath(relativePath);
    if (!assetPath.empty() && AndroidAssetExists(assetPath)
        && std::find(paths.begin(), paths.end(), assetPath) == paths.end()) {
        paths.push_back(assetPath);
    }
#endif

    return paths;
}

std::string FontAssets::manifestPath() {
    const std::vector<std::string> candidates = treeCandidates("fonts/fonts.tsv");
    return candidates.empty() ? std::string() : candidates.front();
}

} // namespace uniscope::d2
