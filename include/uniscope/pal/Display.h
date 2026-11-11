/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_DISPLAY_H
#define UNISCOPE_PAL_DISPLAY_H

#include <string>
#include <vector>

namespace uniscope::pal {

struct DisplayMode {
    int width = 0;
    int height = 0;
    int refreshRate = 0; // 0 = 未知
};

class IDisplayEnumerator {
public:
    virtual ~IDisplayEnumerator() = default;
    virtual int displayCount() const = 0;
    virtual DisplayMode currentMode(int displayIndex) const = 0;
    virtual std::vector<DisplayMode> modes(int displayIndex) const = 0;
    virtual bool supportsExclusiveFullscreen() const = 0;
};

class IUrlOpener {
public:
    virtual ~IUrlOpener() = default;
    virtual bool open(const std::string& url) = 0;
};

} // namespace uniscope::pal

#endif
