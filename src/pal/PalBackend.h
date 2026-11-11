/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_BACKEND_H
#define UNISCOPE_PAL_BACKEND_H

#include "uniscope/pal/Platform.h"

#include <memory>
#include <string>

namespace uniscope::pal {

#if defined(__ANDROID__)
std::unique_ptr<IPlatform> createAndroidPlatform(const PlatformInit& init, std::string* error);
#else
std::unique_ptr<IPlatform> createGlfwPlatform(const PlatformInit& init, std::string* error);
#endif

} // namespace uniscope::pal

#endif
