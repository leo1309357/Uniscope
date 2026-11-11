/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GL_SURFACE_H
#define UNISCOPE_D2_GL_SURFACE_H

#if !defined(__ANDROID__)
struct GLFWwindow;
#endif

namespace uniscope::d2 {

class GlSurface {
public:
    GlSurface(int width, int height, const char* title, int msaaSamples, bool visible);
    ~GlSurface();

    GlSurface(const GlSurface&) = delete;
    GlSurface& operator=(const GlSurface&) = delete;

#if !defined(__ANDROID__)
    GLFWwindow* window() const { return window_; }
    bool valid() const { return window_ != nullptr; }
#else
    bool valid() const { return false; }
#endif

    void makeCurrent();
    void swapBuffers();
    void pollEvents();
    bool shouldClose() const;
    int framebufferWidth() const;
    int framebufferHeight() const;
    float framebufferAspect() const;

private:
#if !defined(__ANDROID__)
    GLFWwindow* window_ = nullptr;
#endif
};

} // namespace uniscope::d2

#endif
