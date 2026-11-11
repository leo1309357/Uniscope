/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

#include <GLFW/glfw3.h>

#include <glad/glad.h>
#include <stb_image.h>

#include "uniscope/pal/Clock.h"
#include "uniscope/pal/Display.h"
#include "uniscope/pal/Log.h"
#include "uniscope/pal/Platform.h"
#include "uniscope/pal/Window.h"

#include "../PalBackend.h"
#include "../common/FileSystemAssetSource.h"
#include "../common/StdPathProvider.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace uniscope::pal {

namespace {

bool gGlfwInitialized = false;

void appendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
        codepoint = 0xFFFD;
    }
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

void setWindowIcon(GLFWwindow* window, const std::string& path) {
    if (path.empty()) {
        return;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    std::FILE* file = nullptr;
#if defined(_WIN32)
    const std::filesystem::path nativePath = std::filesystem::u8path(path);
    file = _wfopen(nativePath.c_str(), L"rb");
#else
    file = std::fopen(path.c_str(), "rb");
#endif
    unsigned char* pixels = file != nullptr
        ? stbi_load_from_file(file, &width, &height, &channels, 4)
        : nullptr;
    if (file != nullptr) {
        std::fclose(file);
    }
    if (pixels == nullptr) {
        return;
    }
    GLFWimage image{width, height, pixels};
    glfwSetWindowIcon(window, 1, &image);
    stbi_image_free(pixels);
}

void applyOpenGlContextHints(int msaaSamples) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, msaaSamples > 0 ? msaaSamples : 0);
}

} // namespace

class GlfwWindowBackend final : public IWindow {
public:
    GlfwWindowBackend(const WindowDesc& desc, std::string* error);
    ~GlfwWindowBackend() override;

    bool valid() const override { return window_ != nullptr; }
    WindowCapabilities capabilities() const override;

    void pollEvents() override { glfwPollEvents(); }
    void waitEventsTimeout(double seconds) override { glfwWaitEventsTimeout(seconds); }
    bool shouldClose() const override;
    void requestClose() override;

    void beginInputFrame() override;
    void clearFrameEdges() override;
    bool isKeyPressed(int key) const override;
    const PointerState& pointer() const override { return pointer_; }
    int touchContactCount() const override { return 0; }
    const TouchContact* touchContacts() const override { return nullptr; }
    int touchContacts(TouchContact* out, int max) const override {
        (void)out;
        (void)max;
        return 0;
    }

    ITextInput* textInput() override { return nullptr; }
    std::string consumeTypedText() override;
    bool consumeBackspace() override;
    void setKeyCaptureEnabled(bool enabled) override;
    int consumeCapturedKey() override;

    int framebufferWidth() const override;
    int framebufferHeight() const override;
    float framebufferAspect() const override;
    float displayDensityDpi() const override { return 0.0f; }
    bool isFocused() const override;
    bool isIconified() const override;
    bool shouldSuspendRendering() const override;

    LifecycleState lifecycle() const override;
    bool pollLifecycleEvent(LifecycleEvent* out) override {
        (void)out;
        return false;
    }

    void setTitle(const std::string& title) override;
    bool setMode(WindowMode mode, int width, int height, int refreshRate) override;
    bool setVsync(bool enabled) override;
    void makeContextCurrent() override;
    void swapBuffers() override;
    double time() const override { return glfwGetTime(); }

    GfxApi gfxApi() const override { return GfxApi::OpenGL33; }
    GfxCapabilities queryGfxCapabilities() override;
    void* nativeHandle() override { return window_; }

private:
    void refreshWindowState();
    void storeWindowedPlacement();

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void windowFocusCallback(GLFWwindow* window, int focused);
    static void windowIconifyCallback(GLFWwindow* window, int iconified);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void scrollCallback(GLFWwindow* window, double xOffset, double yOffset);

    GLFWwindow* window_ = nullptr;
    bool focused_ = true;
    bool iconified_ = false;
    WindowMode displayMode_ = WindowMode::Windowed;
    int windowedWidth_ = 800;
    int windowedHeight_ = 600;
    int windowedPosX_ = 0;
    int windowedPosY_ = 0;
    bool vsync_ = true;
    bool captureKeys_ = false;
    int capturedKey_ = -1;
    std::string typedText_;
    bool backspacePressed_ = false;
    PointerState pointer_{};
};

GlfwWindowBackend::GlfwWindowBackend(const WindowDesc& desc, std::string* error)
    : windowedWidth_(desc.width > 0 ? desc.width : 1280),
      windowedHeight_(desc.height > 0 ? desc.height : 720) {
    if (!gGlfwInitialized && !glfwInit()) {
        if (error != nullptr) {
            *error = "Failed to initialize GLFW";
        }
        return;
    }
    gGlfwInitialized = true;

    glfwDefaultWindowHints();
    applyOpenGlContextHints(desc.msaaSamples);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(
        windowedWidth_,
        windowedHeight_,
        desc.title.empty() ? "Uniscope" : desc.title.c_str(),
        nullptr,
        nullptr);
    glfwDefaultWindowHints();
    if (window_ == nullptr) {
        if (error != nullptr) {
            *error = "Failed to create GLFW window";
        }
        return;
    }

    setWindowIcon(window_, desc.iconPath);

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    vsync_ = true;
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetWindowFocusCallback(window_, windowFocusCallback);
    glfwSetWindowIconifyCallback(window_, windowIconifyCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetCharCallback(window_, charCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetScrollCallback(window_, scrollCallback);
    focused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    iconified_ = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
    glfwGetWindowPos(window_, &windowedPosX_, &windowedPosY_);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        if (error != nullptr) {
            *error = "Failed to initialize GLAD";
        }
        glfwDestroyWindow(window_);
        window_ = nullptr;
        return;
    }

    if (desc.msaaSamples > 0) {
        glEnable(GL_MULTISAMPLE);
    }

    framebufferSizeCallback(window_, framebufferWidth(), framebufferHeight());
}

GlfwWindowBackend::~GlfwWindowBackend() {
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
}

WindowCapabilities GlfwWindowBackend::capabilities() const {
    WindowCapabilities caps;
    caps.supportsResize = true;
    caps.supportsBorderless = true;
    caps.supportsExclusiveFullscreen = true;
    caps.supportsVsyncToggle = true;
    caps.supportsMultipleDisplays = true;
    caps.supportsTouch = false;
    caps.supportsPointerCapture = true;
    caps.supportsIme = false;
    caps.supportsSuspendResume = false;
    caps.maxMsaaSamples = 0;
    return caps;
}

bool GlfwWindowBackend::shouldClose() const {
    return window_ == nullptr || glfwWindowShouldClose(window_);
}

void GlfwWindowBackend::requestClose() {
    if (window_ != nullptr) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool GlfwWindowBackend::isKeyPressed(int key) const {
    return window_ != nullptr && glfwGetKey(window_, key) == GLFW_PRESS;
}

void GlfwWindowBackend::refreshWindowState() {
    if (window_ == nullptr) {
        return;
    }
    focused_ = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    iconified_ = glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
}

void GlfwWindowBackend::beginInputFrame() {
    if (window_ == nullptr) {
        return;
    }

    refreshWindowState();
    double windowX = 0.0;
    double windowY = 0.0;
    glfwGetCursorPos(window_, &windowX, &windowY);

    int windowWidth = 0;
    int windowHeight = 0;
    int bufferWidth = 0;
    int bufferHeight = 0;
    glfwGetWindowSize(window_, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window_, &bufferWidth, &bufferHeight);

    if (windowWidth > 0 && windowHeight > 0 && bufferWidth > 0 && bufferHeight > 0) {
        const double scaleX =
            static_cast<double>(bufferWidth) / static_cast<double>(windowWidth);
        const double scaleY =
            static_cast<double>(bufferHeight) / static_cast<double>(windowHeight);
        pointer_.cursorX = windowX * scaleX;
        pointer_.cursorY = windowY * scaleY;
        pointer_.cursorValid = focused_
            && windowX >= 0.0
            && windowY >= 0.0
            && windowX <= static_cast<double>(windowWidth)
            && windowY <= static_cast<double>(windowHeight);
    } else {
        pointer_.cursorValid = false;
    }

    pointer_.leftDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    pointer_.rightDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    pointer_.middleDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
}

void GlfwWindowBackend::clearFrameEdges() {
    pointer_.scrollX = 0.0;
    pointer_.scrollY = 0.0;
}

std::string GlfwWindowBackend::consumeTypedText() {
    std::string typed;
    typed.swap(typedText_);
    return typed;
}

bool GlfwWindowBackend::consumeBackspace() {
    const bool pressed = backspacePressed_;
    backspacePressed_ = false;
    return pressed;
}

void GlfwWindowBackend::setKeyCaptureEnabled(bool enabled) {
    captureKeys_ = enabled;
    capturedKey_ = -1;
}

int GlfwWindowBackend::consumeCapturedKey() {
    const int key = capturedKey_;
    capturedKey_ = -1;
    return key;
}

int GlfwWindowBackend::framebufferWidth() const {
    int width = 0;
    int height = 0;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &width, &height);
    }
    return width;
}

int GlfwWindowBackend::framebufferHeight() const {
    int width = 0;
    int height = 0;
    if (window_ != nullptr) {
        glfwGetFramebufferSize(window_, &width, &height);
    }
    return height;
}

float GlfwWindowBackend::framebufferAspect() const {
    const int height = framebufferHeight();
    return height > 0 ? static_cast<float>(framebufferWidth()) / static_cast<float>(height) : 1.0f;
}

bool GlfwWindowBackend::isFocused() const {
    if (window_ == nullptr) {
        return false;
    }
    return glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
}

bool GlfwWindowBackend::isIconified() const {
    if (window_ == nullptr) {
        return true;
    }
    return glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE;
}

bool GlfwWindowBackend::shouldSuspendRendering() const {
    if (window_ == nullptr) {
        return true;
    }
    if (glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_TRUE) {
        return true;
    }
    return framebufferWidth() <= 0 || framebufferHeight() <= 0;
}

LifecycleState GlfwWindowBackend::lifecycle() const {
    if (window_ == nullptr || glfwWindowShouldClose(window_)) {
        return LifecycleState::Terminated;
    }
    if (isIconified()) {
        return LifecycleState::Paused;
    }
    return LifecycleState::Active;
}

void GlfwWindowBackend::storeWindowedPlacement() {
    if (window_ == nullptr) {
        return;
    }
    glfwGetWindowPos(window_, &windowedPosX_, &windowedPosY_);
    glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);
}

bool GlfwWindowBackend::setMode(WindowMode mode, int width, int height, int refreshRate) {
    if (window_ == nullptr) {
        return false;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* videoMode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;

    if (displayMode_ == WindowMode::Windowed) {
        storeWindowedPlacement();
    }

    switch (mode) {
    case WindowMode::Windowed: {
        int clampedWidth = width;
        int clampedHeight = height;
        int targetX = windowedPosX_;
        int targetY = windowedPosY_;
        if (monitor != nullptr) {
            int workX = 0;
            int workY = 0;
            int workWidth = 0;
            int workHeight = 0;
            glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);
            if (workWidth > 0 && workHeight > 0) {
                constexpr int kMarginX = 16;
                constexpr int kMarginY = 64;
                clampedWidth = std::max(320, std::min(width, workWidth - kMarginX));
                clampedHeight = std::max(240, std::min(height, workHeight - kMarginY));
                if (clampedWidth != width || clampedHeight != height) {
                    targetX = workX + (workWidth - clampedWidth) / 2;
                    targetY = workY + (workHeight - clampedHeight) / 2;
                }
            }
        }
        windowedWidth_ = clampedWidth;
        windowedHeight_ = clampedHeight;
        glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(
            window_, nullptr, targetX, targetY, clampedWidth, clampedHeight, GLFW_DONT_CARE);
        break;
    }
    case WindowMode::Borderless: {
        if (videoMode != nullptr) {
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);
            glfwSetWindowAttrib(window_, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(
                window_,
                nullptr,
                monitorX,
                monitorY,
                videoMode->width,
                videoMode->height,
                GLFW_DONT_CARE);
        }
        break;
    }
    case WindowMode::Fullscreen: {
        if (monitor != nullptr) {
            const int targetRefresh = refreshRate > 0
                ? refreshRate
                : (videoMode != nullptr ? videoMode->refreshRate : 60);
            const bool alreadyFullscreen = displayMode_ == WindowMode::Fullscreen;
            const bool resolutionChanged =
                framebufferWidth() != width || framebufferHeight() != height;

            if (alreadyFullscreen && resolutionChanged) {
                const int restoreWidth = windowedWidth_ > 0 ? windowedWidth_ : width;
                const int restoreHeight = windowedHeight_ > 0 ? windowedHeight_ : height;
                glfwSetWindowMonitor(
                    window_,
                    nullptr,
                    windowedPosX_,
                    windowedPosY_,
                    restoreWidth,
                    restoreHeight,
                    GLFW_DONT_CARE);
            }

            glfwSetWindowMonitor(window_, monitor, 0, 0, width, height, targetRefresh);
        }
        break;
    }
    }

    displayMode_ = mode;
    framebufferSizeCallback(window_, framebufferWidth(), framebufferHeight());
    return true;
}

bool GlfwWindowBackend::setVsync(bool enabled) {
    if (window_ == nullptr) {
        return false;
    }
    vsync_ = enabled;
    glfwSwapInterval(enabled ? 1 : 0);
    return true;
}

void GlfwWindowBackend::makeContextCurrent() {
    if (window_ == nullptr) {
        return;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(vsync_ ? 1 : 0);
}

void GlfwWindowBackend::swapBuffers() {
    if (window_ != nullptr) {
        glfwSwapBuffers(window_);
    }
}

void GlfwWindowBackend::setTitle(const std::string& title) {
    if (window_ != nullptr) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

GfxCapabilities GlfwWindowBackend::queryGfxCapabilities() {
    GfxCapabilities caps;
    caps.api = GfxApi::OpenGL33;
    if (window_ == nullptr) {
        return caps;
    }

    const auto readString = [](GLenum name) {
        const char* value = reinterpret_cast<const char*>(glGetString(name));
        return value != nullptr ? std::string(value) : std::string();
    };
    caps.vendor = readString(GL_VENDOR);
    caps.renderer = readString(GL_RENDERER);
    caps.version = readString(GL_VERSION);

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    caps.maxMsaaSamples = maxSamples > 0 ? maxSamples : 0;

    caps.maxAnisotropy = 1.0f;
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        caps.maxAnisotropy = maxAniso > 1.0f ? maxAniso : 1.0f;
    }
    return caps;
}

void GlfwWindowBackend::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)window;
    glViewport(0, 0, width, height);
}

void GlfwWindowBackend::windowFocusCallback(GLFWwindow* window, int focused) {
    (void)focused;
    auto* self = static_cast<GlfwWindowBackend*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }
    self->refreshWindowState();
    if (!self->focused_) {
        self->pointer_.cursorValid = false;
        self->pointer_.leftDown = false;
        self->pointer_.rightDown = false;
        self->pointer_.middleDown = false;
    }
}

void GlfwWindowBackend::windowIconifyCallback(GLFWwindow* window, int iconified) {
    auto* self = static_cast<GlfwWindowBackend*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }
    const bool restoring = iconified != GLFW_TRUE;
    self->iconified_ = iconified == GLFW_TRUE;
    if (restoring) {
#if defined(_WIN32)
        glfwFocusWindow(window);
#endif
    }
    self->focused_ = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
    if (!self->focused_) {
        self->pointer_.cursorValid = false;
        self->pointer_.leftDown = false;
        self->pointer_.rightDown = false;
        self->pointer_.middleDown = false;
    }
}

void GlfwWindowBackend::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    auto* self = static_cast<GlfwWindowBackend*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }

    if (action == GLFW_PRESS && key == GLFW_KEY_BACKSPACE) {
        self->backspacePressed_ = true;
    }

    if (!self->captureKeys_) {
        return;
    }
    if (action == GLFW_PRESS && key != GLFW_KEY_UNKNOWN) {
        self->capturedKey_ = key;
    }
}

void GlfwWindowBackend::charCallback(GLFWwindow* window, unsigned int codepoint) {
    auto* self = static_cast<GlfwWindowBackend*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }
    if (codepoint < 0x20 || codepoint == 0x7F) {
        return;
    }
    appendUtf8(self->typedText_, codepoint);
}

void GlfwWindowBackend::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    auto* self = static_cast<GlfwWindowBackend*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }
    const bool pressed = action == GLFW_PRESS;
    switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
        self->pointer_.leftDown = pressed;
        break;
    case GLFW_MOUSE_BUTTON_RIGHT:
        self->pointer_.rightDown = pressed;
        break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        self->pointer_.middleDown = pressed;
        break;
    default:
        break;
    }
}

void GlfwWindowBackend::scrollCallback(GLFWwindow* window, double xOffset, double yOffset) {
    auto* self = static_cast<GlfwWindowBackend*>(glfwGetWindowUserPointer(window));
    if (self == nullptr) {
        return;
    }
    self->pointer_.scrollX += xOffset;
    self->pointer_.scrollY += yOffset;
}

// ---------------------------------------------------------------------------
// Display enumeration
// ---------------------------------------------------------------------------

class GlfwDisplayEnumerator final : public IDisplayEnumerator {
public:
    GlfwDisplayEnumerator() {
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        if (monitors != nullptr && count > 0) {
            monitors_.assign(monitors, monitors + count);
        }
    }

    ~GlfwDisplayEnumerator() override = default;

    int displayCount() const override { return static_cast<int>(monitors_.size()); }

    DisplayMode currentMode(int displayIndex) const override {
        DisplayMode mode;
        if (displayIndex < 0 || displayIndex >= static_cast<int>(monitors_.size())) {
            return mode;
        }
        if (const GLFWvidmode* videoMode =
                glfwGetVideoMode(monitors_[static_cast<std::size_t>(displayIndex)])) {
            mode.width = videoMode->width;
            mode.height = videoMode->height;
            mode.refreshRate = videoMode->refreshRate;
        }
        return mode;
    }

    std::vector<DisplayMode> modes(int displayIndex) const override {
        std::vector<DisplayMode> result;
        if (displayIndex < 0 || displayIndex >= static_cast<int>(monitors_.size())) {
            return result;
        }
        int modeCount = 0;
        const GLFWvidmode* videoModes =
            glfwGetVideoModes(monitors_[static_cast<std::size_t>(displayIndex)], &modeCount);
        if (videoModes == nullptr) {
            return result;
        }
        for (int index = 0; index < modeCount; ++index) {
            result.push_back(
                {videoModes[index].width, videoModes[index].height, videoModes[index].refreshRate});
        }
        return result;
    }

    bool supportsExclusiveFullscreen() const override { return !monitors_.empty(); }

private:
    std::vector<GLFWmonitor*> monitors_;
};

// ---------------------------------------------------------------------------
// URL opener
// ---------------------------------------------------------------------------

class DesktopUrlOpener final : public IUrlOpener {
public:
    bool open(const std::string& url) override {
        if (url.empty()) {
            return false;
        }
#if defined(_WIN32)
        const std::wstring wide = std::filesystem::u8path(url).wstring();
        const HINSTANCE result = ShellExecuteW(
            nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<std::intptr_t>(result) > 32;
#elif defined(__APPLE__)
        const std::string command = "open \"" + url + "\"";
        return std::system(command.c_str()) == 0;
#else
        const std::string command = "xdg-open \"" + url + "\" >/dev/null 2>&1";
        return std::system(command.c_str()) == 0;
#endif
    }
};

// ---------------------------------------------------------------------------
// Platform
// ---------------------------------------------------------------------------

class GlfwPlatform final : public IPlatform {
public:
    explicit GlfwPlatform(PlatformInit init)
        : init_(std::move(init)),
          log_(init_.appName),
          paths_(AppIdentity{init_.appName, init_.organization, {}}) {}

    ~GlfwPlatform() override {
        if (gGlfwInitialized) {
            glfwTerminate();
            gGlfwInitialized = false;
        }
        if (currentPlatform() == this) {
            setCurrentPlatform(nullptr);
        }
    }

    bool initialize(std::string* error) {
        if (gGlfwInitialized) {
            return true;
        }
        glfwSetErrorCallback([](int code, const char* description) {
            std::fprintf(
                stderr,
                "pal/glfw: error %d: %s\n",
                code,
                description != nullptr ? description : "");
        });
        if (!glfwInit()) {
            if (error != nullptr) {
                *error = "Failed to initialize GLFW";
            }
            return false;
        }
        gGlfwInitialized = true;
        return true;
    }

    PlatformKind kind() const override {
#if defined(_WIN32)
        return PlatformKind::Windows;
#elif defined(__APPLE__)
        return PlatformKind::MacOS;
#else
        return PlatformKind::Linux;
#endif
    }

    const PlatformInit& init() const override { return init_; }
    IClock& clock() override { return clock_; }
    ILogSink& log() override { return log_; }

    std::unique_ptr<IWindow> createWindow(
        const WindowDesc& desc,
        std::string* error) override {
        if (!gGlfwInitialized && !initialize(error)) {
            return nullptr;
        }
        auto window = std::make_unique<GlfwWindowBackend>(desc, error);
        if (!window->valid()) {
            return nullptr;
        }
        return window;
    }

    std::unique_ptr<IAssetSource> createAssetSource(
        const AssetSourceDesc& desc,
        std::string* error) override {
        const std::string root = desc.rootPath.empty() ? paths_.paths().resourceDir : desc.rootPath;
        if (root.empty()) {
            if (error != nullptr) {
                *error = "No resource root available";
            }
            return nullptr;
        }
        return std::make_unique<FileSystemAssetSource>(root);
    }

    std::unique_ptr<IPathProvider> createPathProvider() override {
        return std::make_unique<StdPathProvider>(
            AppIdentity{init_.appName, init_.organization, {}});
    }

    std::unique_ptr<IDisplayEnumerator> createDisplayEnumerator() override {
        if (!gGlfwInitialized) {
            return nullptr;
        }
        return std::make_unique<GlfwDisplayEnumerator>();
    }

    std::unique_ptr<IUrlOpener> createUrlOpener() override {
        return std::make_unique<DesktopUrlOpener>();
    }

private:
    PlatformInit init_;
    StdClock clock_;
    StdLogSink log_;
    StdPathProvider paths_;
};

std::unique_ptr<IPlatform> createGlfwPlatform(const PlatformInit& init, std::string* error) {
    auto platform = std::make_unique<GlfwPlatform>(init);
    if (!platform->initialize(error)) {
        return nullptr;
    }
    return platform;
}

} // namespace uniscope::pal
