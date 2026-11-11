/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/Device.h"

#include "uniscope/d2/Context2D.h"
#include "Renderer2D.h"
#include "GlSurface.h"
#include "uniscope/d2/AssetPaths.h"

namespace uniscope::d2 {

Device::Device(DeviceConfig config)
    : config_(std::move(config)) {
    if (!config_.assetRoot.empty()) {
        AssetPaths::setRoot(config_.assetRoot);
    }

    surface_ = std::make_unique<GlSurface>(
        config_.width,
        config_.height,
        config_.title.c_str(),
        config_.msaaSamples,
        config_.visible);
    if (!surface_->valid()) {
        return;
    }

    renderer_ = std::make_unique<Renderer2D>();
    context_ = std::make_unique<Context2D>(*renderer_);
}

Device::~Device() = default;

bool Device::valid() const {
    return surface_ != nullptr && surface_->valid() && renderer_ != nullptr;
}

float Device::framebufferAspect() const {
    return surface_ != nullptr ? surface_->framebufferAspect() : 1.0f;
}

bool Device::shouldClose() const {
    return surface_ == nullptr || surface_->shouldClose();
}

Renderer2D& Device::renderer() {
    return *renderer_;
}

const Renderer2D& Device::renderer() const {
    return *renderer_;
}

Context2D& Device::context() {
    return *context_;
}

const Context2D& Device::context() const {
    return *context_;
}

void Device::beginFrame() {
    surface_->makeCurrent();
    context_->beginFrame();
}

void Device::endFrame() {
    context_->endFrame();
}

void Device::present() {
    swapBuffers();
    pollEvents();
}

void Device::makeContextCurrent() {
    if (surface_ != nullptr) {
        surface_->makeCurrent();
    }
}

void Device::swapBuffers() {
    if (surface_ != nullptr) {
        surface_->swapBuffers();
    }
}

void Device::pollEvents() {
    if (surface_ != nullptr) {
        surface_->pollEvents();
    }
}

} // namespace uniscope::d2
