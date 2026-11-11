/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "GlSurface.h"

#include "../gfx/GlCompat.h"

#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

namespace uniscope::d2 {

#if defined(__ANDROID__)
// Android uses the NativeActivity EGL window (platform/AndroidWindow); the
// GLFW-based offscreen surface is not available. Keep the type linkable so
// unit tests that reference it still build, but report invalid.
GlSurface::GlSurface(int, int, const char*, int, bool) {}
GlSurface::~GlSurface() = default;
void GlSurface::makeCurrent() {}
void GlSurface::swapBuffers() {}
void GlSurface::pollEvents() {}
bool GlSurface::shouldClose() const {
    return true;
}
int GlSurface::framebufferWidth() const {
    return 0;
}
int GlSurface::framebufferHeight() const {
    return 0;
}
#else
GlSurface::GlSurface(int width, int height, const char* title, int msaaSamples, bool visible) {
    if (!glfwInit()) {
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, msaaSamples > 0 ? msaaSamples : 0);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (window_ == nullptr) {
        return;
    }

    makeCurrent();
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

GlSurface::~GlSurface() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

void GlSurface::makeCurrent() {
    if (window_ != nullptr) {
        glfwMakeContextCurrent(window_);
    }
}

void GlSurface::swapBuffers() {
    if (window_ != nullptr) {
        glfwSwapBuffers(window_);
    }
}

void GlSurface::pollEvents() {
    glfwPollEvents();
}

bool GlSurface::shouldClose() const {
    return window_ == nullptr || glfwWindowShouldClose(window_) != 0;
}

int GlSurface::framebufferWidth() const {
    int width = 0;
    int height = 0;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &width, &height);
    }
    return width;
}

int GlSurface::framebufferHeight() const {
    int width = 0;
    int height = 0;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &width, &height);
    }
    return height;
}
#endif

float GlSurface::framebufferAspect() const {
    const int height = framebufferHeight();
    if (height <= 0) {
        return 1.0f;
    }
    return static_cast<float>(framebufferWidth()) / static_cast<float>(height);
}

} // namespace uniscope::d2
