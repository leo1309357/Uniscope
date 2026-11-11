/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_FONT_PLATFORM_PATHS_H
#define UNISCOPE_D2_GFX_FONT_PLATFORM_PATHS_H

#include <string>
#include <vector>

namespace uniscope::d2 {

bool absolutePathExists(const std::string& path);
void appendExistingAbsolutePaths(std::vector<std::string>& paths, const char* const* candidates, std::size_t count);
void appendPlatformDefaultSystemFonts(std::vector<std::string>& paths);

} // namespace uniscope::d2

#endif
