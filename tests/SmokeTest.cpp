/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/Device.h"
#include "uniscope/d2/DeviceConfig.h"

#include <cstdio>
#include <filesystem>

namespace {

std::string findProjectRoot() {
    namespace fs = std::filesystem;
    fs::path cursor = fs::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (fs::exists(cursor / "libs" / "uniscope-2d" / "shaders" / "rect2d.vert")) {
            return cursor.string();
        }
        if (!cursor.has_parent_path()) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return fs::current_path().string();
}

} // namespace

int main() {
    uniscope::d2::DeviceConfig config;
    config.title = "Uniscope2D-Smoke";
    config.width = 960;
    config.height = 540;
    config.visible = false;
    config.assetRoot = findProjectRoot() + "/libs/uniscope-2d";

    uniscope::d2::Device device(config);
    if (!device.valid()) {
        std::fprintf(stderr, "Uniscope2D-Smoke: device init failed\n");
        return 1;
    }

    const float aspect = device.framebufferAspect();
    device.beginFrame();
    device.context().beginRectBatch(aspect);
    device.context().drawRect(0.0f, 0.0f, 0.4f, 0.25f, aspect, 0.2f, 0.6f, 0.9f);
    device.context().drawRectAlpha(0.3f, -0.2f, 0.2f, 0.1f, aspect, 0.9f, 0.4f, 0.2f, 0.7f);
    device.context().flushRectBatch();
    device.endFrame();
    device.present();

    if (device.shouldClose()) {
        return 0;
    }
    std::fprintf(stdout, "Uniscope2D-Smoke: ok\n");
    return 0;
}
