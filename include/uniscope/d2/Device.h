/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_DEVICE_H
#define UNISCOPE_D2_DEVICE_H

#include "uniscope/d2/DeviceConfig.h"
#include "uniscope/d2/Context2D.h"

#include <memory>

namespace uniscope::d2 {

class GlSurface;
class Renderer2D;

class Device {
public:
    explicit Device(DeviceConfig config);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    bool valid() const;
    Renderer2D& renderer();
    const Renderer2D& renderer() const;
    Context2D& context();
    const Context2D& context() const;

    float framebufferAspect() const;
    bool shouldClose() const;

    void beginFrame();
    void endFrame();
    void present();

    void makeContextCurrent();
    void swapBuffers();
    void pollEvents();

private:
    DeviceConfig config_;
    std::unique_ptr<GlSurface> surface_;
    std::unique_ptr<Renderer2D> renderer_;
    std::unique_ptr<Context2D> context_;
};

} // namespace uniscope::d2

#endif
