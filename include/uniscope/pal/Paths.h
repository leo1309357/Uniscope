/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_PATHS_H
#define UNISCOPE_PAL_PATHS_H

#include <string>

namespace uniscope::pal {

struct AppIdentity {
    std::string appName = "App";
    std::string organization = "Org";
    std::string bundleId;
};

struct AppPaths {
    std::string executableDir; // 可执行文件所在目录（Android 为空）
    std::string resourceDir;   // 只读资源根（可为空）
    std::string dataDir;       // 只读游戏数据根（可为空）
    std::string userDataDir;   // 可写，持久（设置/存档）
    std::string cacheDir;      // 可写，可被系统清理
    std::string logDir;        // 可写，日志（可为空）
};

class IPathProvider {
public:
    virtual ~IPathProvider() = default;
    virtual AppIdentity identity() const = 0;
    virtual AppPaths paths() const = 0;
    virtual std::string userDataPath(const std::string& relative) const = 0;
    virtual bool ensureDirectory(const std::string& absolutePath) const = 0;
};

} // namespace uniscope::pal

#endif
