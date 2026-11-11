/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "StdPathProvider.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#endif

namespace uniscope::pal {

namespace {

constexpr const char* kResourcesRoot = "resources";
constexpr const char* kDataRoot = "data";
// Folder names that were used before the rename from "Reverseworks".
constexpr const char* kLegacyFolder = "Reverseworks";

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
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

std::filesystem::path environmentPath(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0'
        ? std::filesystem::u8path(value)
        : std::filesystem::path{};
}

#if defined(_WIN32)
std::filesystem::path winEnvironmentPath(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring value(static_cast<std::size_t>(required), L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), required);
    if (length == 0 || length >= required) {
        return {};
    }
    value.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(value);
}
#endif

// Candidate roots are built the same way the game always built them: walk up
// from the executable, then fall back to the working directory.
std::vector<std::filesystem::path> candidateRoots(const char* leaf) {
    std::vector<std::filesystem::path> candidates;
    std::filesystem::path exeDir = executableDirectory();
    for (int depth = 0; depth < 6 && !exeDir.empty(); ++depth) {
        candidates.push_back(exeDir / leaf);
        exeDir = exeDir.parent_path();
    }
    std::error_code currentError;
    const std::filesystem::path current = std::filesystem::current_path(currentError);
    if (!currentError) {
        candidates.push_back(current / leaf);
    }
    return candidates;
}

bool isDirectory(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

// resources/ is identified by the asset namespace folder inside it.
std::filesystem::path discoverResourcesRoot(const std::string& appName) {
    for (const std::filesystem::path& candidate : candidateRoots(kResourcesRoot)) {
        if (isDirectory(candidate / "assets" / appName)
            || isDirectory(candidate / "assets" / kLegacyFolder)) {
            std::error_code error;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate : canonical;
        }
    }
    return {};
}

// data/ is identified by the game data package folder inside it.
std::filesystem::path discoverDataRoot(const std::string& appName) {
    for (const std::filesystem::path& candidate : candidateRoots(kDataRoot)) {
        if (isDirectory(candidate / appName) || isDirectory(candidate / kLegacyFolder)) {
            std::error_code error;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(candidate, error);
            return error ? candidate : canonical;
        }
    }
    return {};
}

// A probe file is the only honest test: on Windows the permission bits say
// nothing about whether this token can create a file here.
bool directoryAcceptsWrites(const std::filesystem::path& directory) {
    if (directory.empty() || !isDirectory(directory)) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path probe = directory / ".write-probe";
    std::filesystem::remove(probe, error);
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << "probe";
        if (!out) {
            out.close();
            std::filesystem::remove(probe, error);
            return false;
        }
    }
    std::filesystem::remove(probe, error);
    return true;
}

constexpr const char* kAdoptMarker = ".userdata-adopted";
constexpr const char* kCacheFolder = "cache";
// The installer drops a default settings file beside the executable. It is the
// one entry where "the destination already has a file" does not mean the
// player wrote it, so the first adoption replaces it with the real one.
constexpr const char* kDefaultSettingsFile = "settings.cfg";

void copyEntry(const std::filesystem::path& from,
               const std::filesystem::path& to,
               bool replaceExisting) {
    std::error_code error;
    const std::filesystem::copy_options overwrite = replaceExisting
        ? std::filesystem::copy_options::overwrite_existing
        : std::filesystem::copy_options::skip_existing;
    if (std::filesystem::is_directory(from, error) && !error) {
        std::filesystem::copy(
            from,
            to,
            std::filesystem::copy_options::recursive | overwrite,
            error);
        return;
    }
    std::filesystem::copy_file(from, to, overwrite, error);
}

void adoptInto(const std::filesystem::path& destination,
               const std::vector<std::filesystem::path>& sources) {
    if (destination.empty()) {
        return;
    }
    std::error_code error;
    if (std::filesystem::exists(destination / kAdoptMarker, error) && !error) {
        return;
    }
    if (!isDirectory(destination)) {
        std::filesystem::create_directories(destination, error);
        if (error) {
            return;
        }
    }

    const std::filesystem::path target = destination.lexically_normal();
    bool adoptedAnything = false;
    for (const std::filesystem::path& source : sources) {
        if (source.empty() || !isDirectory(source)) {
            continue;
        }
        if (source.lexically_normal() == target) {
            continue;
        }
        adoptedAnything = true;
        std::error_code iterateError;
        for (std::filesystem::directory_iterator entry(source, iterateError), end;
             !iterateError && entry != end;
             entry.increment(iterateError)) {
            const std::filesystem::path& item = entry->path();
            std::error_code nameError;
            const std::string name = item.filename().u8string();
            if (name.empty() || name == kAdoptMarker || name == kCacheFolder) {
                continue;
            }
            if (name.rfind(".write-probe", 0) == 0) {
                continue;
            }
            copyEntry(
                item,
                destination / item.filename(),
                name == kDefaultSettingsFile);
        }
    }

    // Nothing to move: no marker, so a folder that appears later is still
    // adopted. Once something was found the marker stays even if a single file
    // could not be copied - retrying every launch would only hide that save.
    if (!adoptedAnything) {
        return;
    }
    std::ofstream marker(destination / kAdoptMarker, std::ios::binary | std::ios::trunc);
    marker << "adopted\n";
}

} // namespace

StdPathProvider::StdPathProvider(AppIdentity identity, std::string sandboxRoot)
    : identity_(std::move(identity)),
      sandboxRoot_(std::move(sandboxRoot)) {}

void StdPathProvider::initialize() const {
    if (initialized_) {
        return;
    }
    initialized_ = true;

    const std::filesystem::path exeDir = executableDirectory();
    paths_.executableDir = exeDir.u8string();

    std::filesystem::path root;
#if defined(_WIN32)
    std::filesystem::path perUserRoot;
#endif

#if defined(__ANDROID__)
    // Android reads assets straight out of the APK; the filesystem roots stay
    // empty so callers fall back to the asset source.
    paths_.resourceDir.clear();
    paths_.dataDir.clear();

    if (!sandboxRoot_.empty()) {
        root = std::filesystem::u8path(sandboxRoot_);
    } else {
        root = environmentPath("HOME");
        if (root.empty()) {
            std::error_code error;
            root = std::filesystem::temp_directory_path(error);
        }
    }
    if (!root.empty()) {
        const std::string leaf = root.filename().u8string();
        if (leaf != identity_.appName && leaf != kLegacyFolder) {
            root /= identity_.appName;
        }
    }
#else
    std::error_code error;
    const std::filesystem::path resourcesRoot = discoverResourcesRoot(identity_.appName);
    paths_.resourceDir = resourcesRoot.empty()
        ? (exeDir.empty() ? std::string(kResourcesRoot) : (exeDir / kResourcesRoot).u8string())
        : resourcesRoot.u8string();

    const std::filesystem::path dataRoot = discoverDataRoot(identity_.appName);
    if (!dataRoot.empty()) {
        paths_.dataDir = dataRoot.u8string();
    } else {
        const std::filesystem::path current = std::filesystem::current_path(error);
        paths_.dataDir = current.empty()
            ? std::string(kDataRoot)
            : (current / kDataRoot).u8string();
    }

#if defined(_WIN32)
    {
        std::filesystem::path perUser = winEnvironmentPath(L"LOCALAPPDATA");
        if (perUser.empty()) {
            const std::filesystem::path profile = winEnvironmentPath(L"USERPROFILE");
            if (!profile.empty()) {
                perUser = profile / "AppData" / "Local";
            }
        }
        if (!perUser.empty()) {
            perUser /= identity_.appName;
        }
        perUserRoot = perUser;
        // Saves live next to the executable when that folder takes a write,
        // which every per-user install and every unpacked build does. The
        // per-user folder stays the fallback for the layouts that cannot -
        // Program Files, a network share, a hash-checked bundle - because
        // there a portable root would fail every save instead of one.
        root = directoryAcceptsWrites(exeDir) ? exeDir : perUser;
    }
#elif defined(__APPLE__)
    const std::filesystem::path home = environmentPath("HOME");
    if (!home.empty()) {
        root = home / "Library" / "Application Support" / identity_.appName;
    }
#else
    root = environmentPath("XDG_DATA_HOME");
    if (root.empty() || !root.is_absolute()) {
        const std::filesystem::path home = environmentPath("HOME");
        root = home.empty() ? std::filesystem::path{} : home / ".local" / "share";
    }
    if (!root.empty()) {
        root /= identity_.appName;
    }
#endif
#endif

    if (root.empty()) {
        std::error_code fallbackError;
        root = std::filesystem::temp_directory_path(fallbackError);
        if (fallbackError || root.empty()) {
            root = std::filesystem::current_path(fallbackError);
        }
        if (!root.empty()) {
            root /= identity_.appName;
        }
    }

    // Archives written before the rename live in the legacy folder. It is used
    // only while the new one has never been created, so no save is lost.
    if (!root.empty() && root.filename() == identity_.appName) {
        std::error_code existsError;
        const std::filesystem::path legacyRoot = root.parent_path() / kLegacyFolder;
        if (!std::filesystem::exists(root, existsError)
            && std::filesystem::is_directory(legacyRoot, existsError)) {
            root = legacyRoot;
        }
    }

    if (!root.empty()) {
        std::error_code createError;
        std::filesystem::create_directories(root, createError);
    }

#if defined(_WIN32)
    // Moving to a portable root must not look like "the game lost my saves":
    // take what the per-user folder has, once, before the first save is
    // written next to the executable.
    if (!root.empty() && !perUserRoot.empty() && root != perUserRoot) {
        // Least recent first: the legacy folder predates the rename, so the
        // current per-user folder is the one that wins when both have a file.
        std::vector<std::filesystem::path> sources{perUserRoot.parent_path() / kLegacyFolder};
        sources.push_back(perUserRoot);
        adoptInto(root, sources);
    }
#endif

    paths_.userDataDir = root.u8string();
#if defined(__ANDROID__)
    paths_.cacheDir = paths_.userDataDir;
#else
    paths_.cacheDir = (root / "cache").u8string();
#endif
    paths_.logDir = paths_.userDataDir;
}

AppPaths StdPathProvider::paths() const {
    initialize();
    return paths_;
}

std::string StdPathProvider::userDataPath(const std::string& relative) const {
    initialize();
    if (paths_.userDataDir.empty()) {
        return relative;
    }
    std::filesystem::path path = std::filesystem::u8path(paths_.userDataDir);
    path /= std::filesystem::u8path(relative);
    return path.u8string();
}

bool StdPathProvider::ensureDirectory(const std::string& absolutePath) const {
    if (absolutePath.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::u8path(absolutePath), error);
    return !error;
}

bool isWritableDirectory(const std::string& directory) {
    return directoryAcceptsWrites(std::filesystem::u8path(directory));
}

void adoptUserData(const std::string& destination, const std::vector<std::string>& sources) {
    std::vector<std::filesystem::path> paths;
    paths.reserve(sources.size());
    for (const std::string& source : sources) {
        if (!source.empty()) {
            paths.push_back(std::filesystem::u8path(source));
        }
    }
    adoptInto(std::filesystem::u8path(destination), paths);
}

} // namespace uniscope::pal
