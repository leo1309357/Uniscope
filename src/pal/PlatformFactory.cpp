/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/pal/Platform.h"

#include "PalBackend.h"

namespace uniscope::pal {

namespace {
IPlatform* gCurrentPlatform = nullptr;
} // namespace

IPlatform* currentPlatform() {
    return gCurrentPlatform;
}

void setCurrentPlatform(IPlatform* platform) {
    gCurrentPlatform = platform;
}

std::unique_ptr<IPlatform> createPlatform(const PlatformInit& init, std::string* error) {
#if defined(__ANDROID__)
    std::unique_ptr<IPlatform> platform = createAndroidPlatform(init, error);
#else
    std::unique_ptr<IPlatform> platform = createGlfwPlatform(init, error);
#endif
    if (platform) {
        setCurrentPlatform(platform.get());
    }
    return platform;
}

} // namespace uniscope::pal
