/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_DEVICE_CONFIG_H
#define UNISCOPE_D2_DEVICE_CONFIG_H

#include <string>

namespace uniscope::d2 {

struct DeviceConfig {
    std::string assetRoot;
    std::string title = "Uniscope2D";
    int width = 1280;
    int height = 720;
    int msaaSamples = 0;
    bool visible = true;
};

} // namespace uniscope::d2

#endif
