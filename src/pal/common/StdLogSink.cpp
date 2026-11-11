/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/pal/Log.h"

#include <cstdio>
#include <utility>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace uniscope::pal {

StdLogSink::StdLogSink(std::string appName)
    : appName_(std::move(appName)) {}

void StdLogSink::write(LogLevel level, const char* category, const char* message) {
#if defined(__ANDROID__)
    int priority = ANDROID_LOG_INFO;
    switch (level) {
    case LogLevel::Trace: priority = ANDROID_LOG_VERBOSE; break;
    case LogLevel::Debug: priority = ANDROID_LOG_DEBUG; break;
    case LogLevel::Info: priority = ANDROID_LOG_INFO; break;
    case LogLevel::Warning: priority = ANDROID_LOG_WARN; break;
    case LogLevel::Error: priority = ANDROID_LOG_ERROR; break;
    }
    __android_log_print(
        priority,
        appName_.c_str(),
        "[%s] %s",
        category != nullptr ? category : "pal",
        message != nullptr ? message : "");
#else
    std::FILE* stream = level == LogLevel::Warning || level == LogLevel::Error
        ? stderr
        : stdout;
    std::fprintf(
        stream,
        "[%s][%s] %s\n",
        appName_.c_str(),
        category != nullptr ? category : "pal",
        message != nullptr ? message : "");
#endif
}

} // namespace uniscope::pal
