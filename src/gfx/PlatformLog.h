/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_PLATFORM_LOG_H
#define UNISCOPE_D2_GFX_PLATFORM_LOG_H

// Renderer diagnostics historically went to stderr, which is a black hole on
// Android (NativeActivity stderr is /dev/null and log.redirect-stdio needs a
// rooted build). On Android these macros forward to logcat under the
// "Eraphanis" tag so shader/font init failures are actually visible; on the
// desktop platforms they keep writing to stderr/stdout exactly as before.

#if defined(__ANDROID__)
#include <android/log.h>
#define UNISCOPE_D2_LOG_ERROR(fmt, ...) \
    __android_log_print(ANDROID_LOG_ERROR, "Eraphanis", fmt, ##__VA_ARGS__)
#define UNISCOPE_D2_LOG_INFO(fmt, ...) \
    __android_log_print(ANDROID_LOG_INFO, "Eraphanis", fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define UNISCOPE_D2_LOG_ERROR(fmt, ...) std::fprintf(stderr, fmt, ##__VA_ARGS__)
#define UNISCOPE_D2_LOG_INFO(fmt, ...) std::fprintf(stdout, fmt, ##__VA_ARGS__)
#endif

#endif
