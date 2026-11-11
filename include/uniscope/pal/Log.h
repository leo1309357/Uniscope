/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_LOG_H
#define UNISCOPE_PAL_LOG_H

#include "Platform.h"

namespace uniscope::pal {

class ILogSink {
public:
    virtual ~ILogSink() = default;
    // category 由调用方给出（"gfx" / "audio" / "pal"），引擎不再硬编码游戏名。
    virtual void write(LogLevel level, const char* category, const char* message) = 0;
};

// 默认实现：桌面写 stderr/stdout，Android 写 logcat（tag = PlatformInit.appName）。
class StdLogSink final : public ILogSink {
public:
    explicit StdLogSink(std::string appName);
    void write(LogLevel level, const char* category, const char* message) override;

private:
    std::string appName_;
};

} // namespace uniscope::pal

#endif
