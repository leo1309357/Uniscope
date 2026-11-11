/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#if defined(__ANDROID__)

#include "uniscope/pal/Assets.h"
#include "uniscope/pal/Clock.h"
#include "uniscope/pal/Display.h"
#include "uniscope/pal/Log.h"
#include "uniscope/pal/Paths.h"
#include "uniscope/pal/Platform.h"
#include "uniscope/pal/Window.h"

#include "../PalBackend.h"
#include "../common/StdPathProvider.h"

#include "../../gfx/AndroidAssetReader.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// GL_EXT_texture_filter_anisotropic enums are not part of the ES headers; the
// values match the extension's.
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace uniscope::pal {

namespace {

constexpr EGLint kContextClientVersion = 3;
constexpr const char* kLogTag = "uniscope.pal";
constexpr std::size_t kKeyStateCount = 512;

int toPalKey(int32_t akey) {
    // Printable ASCII is contiguous, so letters and digits are ranges. This is
    // also what makes text entry work: a name field needs the whole alphabet.
    if (akey >= AKEYCODE_A && akey <= AKEYCODE_Z) {
        return 'A' + (akey - AKEYCODE_A);
    }
    if (akey >= AKEYCODE_0 && akey <= AKEYCODE_9) {
        return '0' + (akey - AKEYCODE_0);
    }
    switch (akey) {
    case AKEYCODE_SPACE: return key::Space;
    case AKEYCODE_MINUS: return '-';
    case AKEYCODE_PERIOD: return key::Period;
    case AKEYCODE_COMMA: return key::Comma;
    case AKEYCODE_DEL: return key::Backspace;
    case AKEYCODE_ENTER:
    case AKEYCODE_DPAD_CENTER: return key::Enter;
    case AKEYCODE_ESCAPE:
    case AKEYCODE_BACK: return key::Escape;
    case AKEYCODE_SHIFT_LEFT: return key::LeftShift;
    case AKEYCODE_SHIFT_RIGHT: return key::RightShift;
    case AKEYCODE_DPAD_UP: return key::Up;
    case AKEYCODE_DPAD_DOWN: return key::Down;
    case AKEYCODE_DPAD_LEFT: return key::Left;
    case AKEYCODE_DPAD_RIGHT: return key::Right;
    case AKEYCODE_PAGE_UP: return key::PageUp;
    case AKEYCODE_PAGE_DOWN: return key::PageDown;
    default: return key::Unknown;
    }
}

int translateLooperTimeout(double seconds) {
    if (seconds <= 0.0) {
        return 0;
    }
    const double ms = seconds * 1000.0;
    if (ms >= static_cast<double>(INT32_MAX)) {
        return INT32_MAX;
    }
    return static_cast<int>(std::llround(ms));
}

std::string normalizeAssetPath(const std::string& logicalPath) {
    std::string normalized = logicalPath;
    for (char& ch : normalized) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    while (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }
    const std::string prefix = "assets/";
    if (normalized.compare(0, prefix.size(), prefix) == 0) {
        normalized.erase(0, prefix.size());
    }
    return normalized;
}

// ---------------------------------------------------------------------------
// APK asset source
// ---------------------------------------------------------------------------

class ApkAssetStream final : public IAssetStream {
public:
    explicit ApkAssetStream(AAsset* asset) : asset_(asset) {}

    ~ApkAssetStream() override {
        if (asset_ != nullptr) {
            AAsset_close(asset_);
        }
    }

    bool read(void* dst, size_t bytes) override {
        if (asset_ == nullptr || dst == nullptr) {
            return false;
        }
        return AAsset_read(asset_, dst, bytes) == static_cast<int>(bytes);
    }

    bool seek(size_t offset) override {
        if (asset_ == nullptr) {
            return false;
        }
        return AAsset_seek(asset_, static_cast<off_t>(offset), SEEK_SET) >= 0;
    }

    size_t size() const override {
        return asset_ != nullptr ? static_cast<size_t>(AAsset_getLength(asset_)) : 0;
    }

private:
    AAsset* asset_ = nullptr;
};

class AndroidAssetSource final : public IAssetSource {
public:
    explicit AndroidAssetSource(AAssetManager* manager) : manager_(manager) {}

    bool exists(const std::string& logicalPath) const override {
        return open(logicalPath, AASSET_MODE_STREAMING) != nullptr;
    }

    bool readText(const std::string& logicalPath, std::string* out) const override {
        if (out == nullptr) {
            return false;
        }
        std::vector<uint8_t> bytes;
        if (!readBytes(logicalPath, &bytes)) {
            return false;
        }
        out->assign(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size());
        return true;
    }

    bool readBytes(const std::string& logicalPath, std::vector<uint8_t>* out) const override {
        if (out == nullptr) {
            return false;
        }
        AAsset* asset = open(logicalPath, AASSET_MODE_BUFFER);
        if (asset == nullptr) {
            return false;
        }
        const std::size_t length = static_cast<std::size_t>(AAsset_getLength(asset));
        out->assign(length, 0);
        const bool ok = length == 0
            || AAsset_read(asset, out->data(), length) == static_cast<int>(length);
        AAsset_close(asset);
        return ok;
    }

    std::unique_ptr<IAssetStream> openStream(const std::string& logicalPath) const override {
        AAsset* asset = open(logicalPath, AASSET_MODE_RANDOM);
        if (asset == nullptr) {
            return nullptr;
        }
        return std::make_unique<ApkAssetStream>(asset);
    }

    bool listDirectory(const std::string& logicalPath, std::vector<std::string>* out) const override {
        if (out == nullptr || manager_ == nullptr) {
            return false;
        }
        out->clear();
        const std::string assetPath = normalizeAssetPath(logicalPath);
        if (assetPath.empty()) {
            return false;
        }
        AAssetDir* directory = AAssetManager_openDir(manager_, assetPath.c_str());
        if (directory == nullptr) {
            return false;
        }
        while (const char* entry = AAssetDir_getNextFileName(directory)) {
            out->push_back(entry);
        }
        AAssetDir_close(directory);
        return true;
    }

    bool resolveToOsPath(const std::string&, std::string*) const override { return false; }

    const char* kindName() const override { return "apk"; }

private:
    AAsset* open(const std::string& logicalPath, int mode) const {
        if (manager_ == nullptr) {
            return nullptr;
        }
        const std::string assetPath = normalizeAssetPath(logicalPath);
        if (assetPath.empty()) {
            return nullptr;
        }
        return AAssetManager_open(manager_, assetPath.c_str(), mode);
    }

    AAssetManager* manager_ = nullptr;
};

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

class AndroidWindowBackend final : public IWindow {
public:
    AndroidWindowBackend(android_app* app, const WindowDesc& desc)
        : app_(app),
          msaaSamples_(desc.msaaSamples),
          width_(desc.width > 0 ? desc.width : 1280),
          height_(desc.height > 0 ? desc.height : 720) {
        keys_.fill(false);
        // Panel density for the physical-size touch layout. AConfiguration
        // hands back either a real dpi or a sentinel; keep only real values.
        if (app_ != nullptr && app_->config != nullptr) {
            const int32_t density = AConfiguration_getDensity(app_->config);
            if (density > 0 && density < ACONFIGURATION_DENSITY_ANY) {
                densityDpi_ = static_cast<float>(density);
            }
        }
        hasWindow_ = app_ != nullptr && app_->window != nullptr;
        // Interactive fullscreen: keep the panel awake for a session that is
        // actually being played, and keep the status bar out of the touch
        // area. The activity theme already requests fullscreen; the flag is
        // the runtime half of it (and the keep-awake is new - without it the
        // display dims mid-scene, since no player input reaches the OS).
        if (hasWindow_ && app_->activity != nullptr) {
            ANativeActivity_setWindowFlags(
                app_->activity,
                AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN,
                0);
        }
        // Same contract as the GLFW backend: creating a window makes its GL
        // context current. The game constructs its renderer (and creates all
        // of its GL resources) as part of the Application object, before it
        // ever calls makeContextCurrent(), so on Android those calls used to
        // run with no current context and fail silently - leaving a renderer
        // with no shaders that only presented a black surface.
        if (hasWindow_) {
            ensureEgl();
        }
    }

    ~AndroidWindowBackend() override { destroyEgl(); }

    bool valid() const override { return app_ != nullptr; }

    WindowCapabilities capabilities() const override {
        WindowCapabilities caps;
        caps.supportsResize = false;
        caps.supportsBorderless = true;
        caps.supportsExclusiveFullscreen = false;
        caps.supportsVsyncToggle = false;
        caps.supportsTouch = true;
        caps.supportsPointerCapture = false;
        caps.supportsIme = false;
        caps.supportsSuspendResume = true;
        caps.maxMsaaSamples = msaaSamples_;
        return caps;
    }

    void pollEvents() override {
        if (app_ == nullptr) {
            return;
        }
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(0, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) {
                source->process(app_, source);
            }
            if (app_->destroyRequested != 0) {
                return;
            }
        }
        refreshWindowState();
    }

    void waitEventsTimeout(double seconds) override {
        if (app_ == nullptr) {
            return;
        }
        int events = 0;
        android_poll_source* source = nullptr;
        ALooper_pollOnce(
            translateLooperTimeout(seconds),
            nullptr,
            &events,
            reinterpret_cast<void**>(&source));
        if (source != nullptr) {
            source->process(app_, source);
        }
        refreshWindowState();
    }

    bool shouldClose() const override {
        return closeRequested_ || (app_ != nullptr && app_->destroyRequested != 0);
    }

    void requestClose() override {
        closeRequested_ = true;
        if (app_ != nullptr && app_->activity != nullptr) {
            ANativeActivity_finish(app_->activity);
        }
    }

    void beginInputFrame() override {
        refreshWindowState();
        // Touch state is pushed by the callbacks on the same thread; validity
        // follows focus + window presence (mirrors the desktop behaviour where
        // an unfocused window reports no cursor).
        if (!focused_ || paused_ || !hasWindow_) {
            pointer_.cursorValid = false;
        } else if (pointer_.leftDown || pointer_.cursorX != 0.0 || pointer_.cursorY != 0.0) {
            pointer_.cursorValid = true;
        }
    }

    void clearFrameEdges() override {
        pointer_.scrollX = 0.0;
        pointer_.scrollY = 0.0;
        for (ContactSlot& slot : contacts_) {
            slot.pressed = false;
            if (slot.released) {
                slot.released = false;
                slot.active = false;
                slot.androidId = -1;
            }
        }
        refreshPrimaryPointer();
    }

    bool isKeyPressed(int keyCode) const override {
        if (keyCode < 0 || keyCode >= static_cast<int>(keys_.size())) {
            return false;
        }
        return keys_[static_cast<std::size_t>(keyCode)];
    }

    const PointerState& pointer() const override { return pointer_; }

    int touchContactCount() const override {
        int count = 0;
        for (const ContactSlot& slot : contacts_) {
            if (slot.active || slot.released) {
                ++count;
            }
        }
        return count;
    }

    const TouchContact* touchContacts() const override {
        refreshContactsView();
        return contactsView_.data();
    }

    int touchContacts(TouchContact* out, int max) const override {
        if (out == nullptr || max <= 0) {
            return 0;
        }
        int count = 0;
        for (const ContactSlot& slot : contacts_) {
            if (!slot.active || count >= max) {
                continue;
            }
            TouchContact& contact = out[count++];
            contact.id = slot.androidId;
            contact.x = slot.x;
            contact.y = slot.y;
            contact.down = true;
            contact.pressed = slot.pressed;
            contact.released = false;
        }
        // Released-this-frame contacts are still reported once so gestures can
        // observe the lift edge, marked down=false.
        for (const ContactSlot& slot : contacts_) {
            if (slot.active || !slot.released || count >= max) {
                continue;
            }
            TouchContact& contact = out[count++];
            contact.id = slot.androidId;
            contact.x = slot.x;
            contact.y = slot.y;
            contact.down = false;
            contact.pressed = false;
            contact.released = true;
        }
        return count;
    }

    ITextInput* textInput() override { return nullptr; }

    std::string consumeTypedText() override {
        std::string typed;
        typed.swap(typedText_);
        return typed;
    }

    bool consumeBackspace() override {
        const bool pressed = backspacePressed_;
        backspacePressed_ = false;
        return pressed;
    }

    void setKeyCaptureEnabled(bool enabled) override {
        captureKeys_ = enabled;
        capturedKey_ = key::Unknown;
    }

    int consumeCapturedKey() override {
        const int captured = capturedKey_;
        capturedKey_ = key::Unknown;
        return captured;
    }

    int framebufferWidth() const override { return width_; }
    int framebufferHeight() const override { return height_; }

    float framebufferAspect() const override {
        return height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
    }

    float displayDensityDpi() const override { return densityDpi_; }
    bool isFocused() const override { return focused_ && !paused_; }
    bool isIconified() const override { return paused_ || !hasWindow_; }

    bool shouldSuspendRendering() const override {
        if (closeRequested_ || (app_ != nullptr && app_->destroyRequested != 0)) {
            return true;
        }
        if (paused_ || !hasWindow_ || app_ == nullptr || app_->window == nullptr) {
            return true;
        }
        return width_ <= 0 || height_ <= 0;
    }

    LifecycleState lifecycle() const override {
        if (closeRequested_ || (app_ != nullptr && app_->destroyRequested != 0)) {
            return LifecycleState::Terminated;
        }
        if (paused_) {
            return LifecycleState::Paused;
        }
        if (!hasWindow_) {
            return LifecycleState::SurfaceLost;
        }
        return LifecycleState::Active;
    }

    bool pollLifecycleEvent(LifecycleEvent* out) override {
        if (out == nullptr || lifecycleEvents_.empty()) {
            return false;
        }
        *out = lifecycleEvents_.front();
        lifecycleEvents_.erase(lifecycleEvents_.begin());
        return true;
    }

    void setTitle(const std::string&) override {
        // No window title on Android; the app runs in immersive mode.
    }

    bool setMode(WindowMode, int width, int height, int) override {
        // Fixed fullscreen surface; keep the requested size only as a layout
        // hint for when the real surface size is not known yet.
        if (width > 0 && height > 0 && (width_ <= 0 || height_ <= 0)) {
            width_ = width;
            height_ = height;
        }
        return true;
    }

    bool setVsync(bool enabled) override {
        if (display_ == EGL_NO_DISPLAY) {
            return false;
        }
        return eglSwapInterval(display_, enabled ? 1 : 0) == EGL_TRUE;
    }

    void makeContextCurrent() override {
        ensureEgl();
        if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE
            && context_ != EGL_NO_CONTEXT) {
            eglMakeCurrent(display_, surface_, surface_, context_);
        }
    }

    void swapBuffers() override {
        if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
            eglSwapBuffers(display_, surface_);
        }
        // Surface silent GL failures once per ~second: a bad draw call
        // otherwise leaves no trace on Android (stderr is a black hole).
        if (++framesSinceErrorCheck_ >= 60) {
            framesSinceErrorCheck_ = 0;
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                __android_log_print(
                    ANDROID_LOG_ERROR, kLogTag, "GL error 0x%04x after frame", error);
            }
        }
    }

    double time() const override {
        using Clock = std::chrono::steady_clock;
        static const auto start = Clock::now();
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    GfxApi gfxApi() const override { return GfxApi::OpenGLES30; }

    GfxCapabilities queryGfxCapabilities() override {
        GfxCapabilities caps;
        caps.api = GfxApi::OpenGLES30;
        if (context_ == EGL_NO_CONTEXT) {
            return caps;
        }
        if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
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
        GLint extCount = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extCount);
        bool hasAniso = false;
        for (GLint index = 0; index < extCount; ++index) {
            const char* ext = reinterpret_cast<const char*>(
                glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
            if (ext != nullptr && std::strcmp(ext, "GL_EXT_texture_filter_anisotropic") == 0) {
                hasAniso = true;
                break;
            }
        }
        if (hasAniso) {
            GLfloat maxAniso = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
            caps.maxAnisotropy = maxAniso > 1.0f ? maxAniso : 1.0f;
        }
        return caps;
    }

    void* nativeHandle() override { return app_ != nullptr ? app_->window : nullptr; }

    // ---- android_app callbacks (same thread) ----
    void onWindowCreated() {
        hasWindow_ = true;
        // A recreated surface (resume, rotation) loses the window flags too.
        if (app_ != nullptr && app_->activity != nullptr) {
            ANativeActivity_setWindowFlags(
                app_->activity,
                AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN,
                0);
        }
        ensureEgl();
        lifecycleEvents_.push_back({LifecycleState::Active, true});
    }

    void onWindowDestroyed() {
        destroyEglSurface();
        lifecycleEvents_.push_back({LifecycleState::SurfaceLost, false});
    }

    void onGainedFocus() {
        focused_ = true;
        lifecycleEvents_.push_back({LifecycleState::Active, true});
    }

    void onLostFocus() {
        focused_ = false;
        pointer_.cursorValid = false;
        pointer_.leftDown = false;
        pointer_.rightDown = false;
        pointer_.middleDown = false;
        keys_.fill(false);
        clearContacts();
        lifecycleEvents_.push_back({LifecycleState::Paused, false});
    }

    void onPaused() {
        paused_ = true;
        pointer_.cursorValid = false;
        pointer_.leftDown = false;
        keys_.fill(false);
        clearContacts();
        lifecycleEvents_.push_back({LifecycleState::Paused, false});
    }

    void onResumed() {
        paused_ = false;
        lifecycleEvents_.push_back({LifecycleState::Active, true});
    }

    void onMotionPointer(
        int androidId, float x, float y, bool phaseDown, bool isPress, bool isRelease) {
        // Scale touch pixels to framebuffer pixels (usually 1:1; surface size
        // equals window size on modern Android).
        float fbX = std::max(0.0f, x);
        float fbY = std::max(0.0f, y);
        if (width_ > 0 && height_ > 0 && app_ != nullptr && app_->window != nullptr) {
            const int32_t winW = ANativeWindow_getWidth(app_->window);
            const int32_t winH = ANativeWindow_getHeight(app_->window);
            if (winW > 0 && winH > 0) {
                fbX = x * static_cast<float>(width_) / static_cast<float>(winW);
                fbY = y * static_cast<float>(height_) / static_cast<float>(winH);
            }
        }

        ContactSlot* slot = nullptr;
        for (ContactSlot& candidate : contacts_) {
            if (candidate.active && candidate.androidId == androidId) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr && phaseDown) {
            for (ContactSlot& candidate : contacts_) {
                if (!candidate.active && !candidate.released) {
                    slot = &candidate;
                    break;
                }
            }
        }
        if (slot == nullptr) {
            refreshPrimaryPointer();
            return;
        }
        if (phaseDown) {
            const bool isNew = !slot->active;
            slot->active = true;
            slot->androidId = androidId;
            slot->x = fbX;
            slot->y = fbY;
            slot->pressed = slot->pressed || isPress || isNew;
            slot->released = false;
        } else {
            slot->x = fbX;
            slot->y = fbY;
            if (slot->active) {
                slot->released = slot->released || isRelease || !isPress;
            }
            slot->active = false;
        }
        refreshPrimaryPointer();
        pointer_.cursorValid = focused_ && !paused_;
    }

    void onScroll(float xOffset, float yOffset) {
        pointer_.scrollX += xOffset;
        pointer_.scrollY += yOffset;
    }

    void onKey(int rwKey, bool down) {
        if (rwKey >= 0 && rwKey < static_cast<int>(keys_.size())) {
            keys_[static_cast<std::size_t>(rwKey)] = down;
        }
        if (captureKeys_ && down && rwKey >= 0) {
            capturedKey_ = rwKey;
        }

        // Android delivers key codes rather than characters, so the printable
        // ASCII range is turned into text here and the shift state is tracked
        // to get the case right. Repeats are typed out as well, which is what a
        // person expects from a held letter.
        if (!down) {
            if (rwKey == key::LeftShift || rwKey == key::RightShift) {
                shiftHeld_ = false;
            }
            return;
        }
        if (rwKey == key::LeftShift || rwKey == key::RightShift) {
            shiftHeld_ = true;
            return;
        }
        if (rwKey == key::Backspace) {
            backspacePressed_ = true;
            return;
        }
        if (rwKey >= 32 && rwKey <= 126) {
            char typed = static_cast<char>(rwKey);
            if (rwKey >= 'A' && rwKey <= 'Z' && !shiftHeld_) {
                typed = static_cast<char>(rwKey + 32);
            }
            typedText_.push_back(typed);
        }
    }

private:
    struct ContactSlot {
        bool active = false;
        int androidId = -1;
        float x = 0.0f;
        float y = 0.0f;
        bool pressed = false;
        bool released = false;
    };

    bool ensureEgl() {
        if (app_ == nullptr || app_->window == nullptr) {
            return false;
        }
        if (display_ == EGL_NO_DISPLAY) {
            display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (display_ == EGL_NO_DISPLAY || eglInitialize(display_, nullptr, nullptr) != EGL_TRUE) {
                display_ = EGL_NO_DISPLAY;
                return false;
            }
        }
        if (config_ == nullptr) {
            const EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 16,
                EGL_STENCIL_SIZE, 0,
                EGL_SAMPLE_BUFFERS, msaaSamples_ > 0 ? 1 : 0,
                EGL_SAMPLES, msaaSamples_ > 0 ? msaaSamples_ : 0,
                EGL_NONE,
            };
            EGLConfig configs[32];
            EGLint count = 0;
            // MSAA may be unsupported; retry without multisampling instead of
            // failing context creation outright (common on low-end Mali/Adreno).
            if (eglChooseConfig(display_, attribs, configs, 32, &count) != EGL_TRUE || count <= 0) {
                const EGLint fallback[] = {
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                    EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8,
                    EGL_BLUE_SIZE, 8,
                    EGL_ALPHA_SIZE, 8,
                    EGL_DEPTH_SIZE, 16,
                    EGL_STENCIL_SIZE, 0,
                    EGL_NONE,
                };
                if (eglChooseConfig(display_, fallback, configs, 32, &count) != EGL_TRUE || count <= 0) {
                    return false;
                }
                msaaSamples_ = 0;
            }
            config_ = configs[0];
        }
        if (context_ == EGL_NO_CONTEXT) {
            const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, kContextClientVersion, EGL_NONE};
            context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctxAttribs);
            if (context_ == EGL_NO_CONTEXT) {
                return false;
            }
        }
        if (surface_ == EGL_NO_SURFACE) {
            surface_ = eglCreateWindowSurface(display_, config_, app_->window, nullptr);
            if (surface_ == EGL_NO_SURFACE) {
                return false;
            }
        }
        if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
            return false;
        }
        eglSwapInterval(display_, 1);

        EGLint width = 0;
        EGLint height = 0;
        if (eglQuerySurface(display_, surface_, EGL_WIDTH, &width) == EGL_TRUE && width > 0) {
            width_ = width;
        }
        if (eglQuerySurface(display_, surface_, EGL_HEIGHT, &height) == EGL_TRUE && height > 0) {
            height_ = height;
        }
        hasWindow_ = true;
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "EGL ready: vendor=%s renderer=%s version=%s",
            reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)));
        return true;
    }

    void destroyEglSurface() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        }
        if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        surface_ = EGL_NO_SURFACE;
        hasWindow_ = false;
    }

    void destroyEgl() {
        destroyEglSurface();
        if (display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        context_ = EGL_NO_CONTEXT;
        config_ = nullptr;
        if (display_ != EGL_NO_DISPLAY) {
            eglTerminate(display_);
        }
        display_ = EGL_NO_DISPLAY;
    }

    void refreshWindowState() {
        if (app_ == nullptr) {
            return;
        }
        if (app_->window == nullptr) {
            if (hasWindow_) {
                destroyEglSurface();
            }
            return;
        }
        if (!hasWindow_) {
            ensureEgl();
        }

        // Rotation and multi-window changes resize the surface without a new
        // window, so the cached size must follow the EGL surface each poll.
        int newWidth = 0;
        int newHeight = 0;
        if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
            EGLint surfaceWidth = 0;
            EGLint surfaceHeight = 0;
            if (eglQuerySurface(display_, surface_, EGL_WIDTH, &surfaceWidth) == EGL_TRUE
                && eglQuerySurface(display_, surface_, EGL_HEIGHT, &surfaceHeight) == EGL_TRUE
                && surfaceWidth > 0 && surfaceHeight > 0) {
                newWidth = surfaceWidth;
                newHeight = surfaceHeight;
            }
        }
        if (newWidth <= 0 || newHeight <= 0) {
            newWidth = ANativeWindow_getWidth(app_->window);
            newHeight = ANativeWindow_getHeight(app_->window);
        }
        if (newWidth > 0 && newHeight > 0 && (newWidth != width_ || newHeight != height_)) {
            width_ = newWidth;
            height_ = newHeight;
        }
    }

    void clearContacts() {
        for (ContactSlot& slot : contacts_) {
            slot.active = false;
            slot.pressed = false;
            slot.released = false;
            slot.androidId = -1;
        }
    }

    void refreshPrimaryPointer() {
        const ContactSlot* primary = nullptr;
        for (const ContactSlot& slot : contacts_) {
            if (slot.active) {
                primary = &slot;
                break;
            }
        }
        if (primary != nullptr) {
            pointer_.cursorX = primary->x;
            pointer_.cursorY = primary->y;
            pointer_.cursorValid = focused_ && !paused_;
            pointer_.leftDown = true;
        } else {
            pointer_.leftDown = false;
        }
    }

    void refreshContactsView() const {
        TouchContact* out = contactsView_.data();
        int count = 0;
        for (const ContactSlot& slot : contacts_) {
            if (!slot.active || count >= kMaxTouchContacts) {
                continue;
            }
            TouchContact& contact = out[count++];
            contact.id = slot.androidId;
            contact.x = slot.x;
            contact.y = slot.y;
            contact.down = true;
            contact.pressed = slot.pressed;
            contact.released = false;
        }
        for (const ContactSlot& slot : contacts_) {
            if (slot.active || !slot.released || count >= kMaxTouchContacts) {
                continue;
            }
            TouchContact& contact = out[count++];
            contact.id = slot.androidId;
            contact.x = slot.x;
            contact.y = slot.y;
            contact.down = false;
            contact.pressed = false;
            contact.released = true;
        }
    }

    android_app* app_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig config_ = nullptr;
    int msaaSamples_ = 0;
    int width_ = 0;
    int height_ = 0;
    float densityDpi_ = 0.0f;
    bool closeRequested_ = false;
    bool focused_ = true;
    bool paused_ = false;
    bool hasWindow_ = false;
    bool captureKeys_ = false;
    int capturedKey_ = key::Unknown;
    std::string typedText_;
    bool backspacePressed_ = false;
    bool shiftHeld_ = false;
    PointerState pointer_{};
    std::array<bool, kKeyStateCount> keys_{};
    std::array<ContactSlot, kMaxTouchContacts> contacts_{};
    mutable std::array<TouchContact, kMaxTouchContacts> contactsView_{};
    int framesSinceErrorCheck_ = 0;
    std::vector<LifecycleEvent> lifecycleEvents_;
};

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

class AndroidDisplayEnumerator final : public IDisplayEnumerator {
public:
    explicit AndroidDisplayEnumerator(android_app* app) : app_(app) {}

    int displayCount() const override { return 1; }

    DisplayMode currentMode(int displayIndex) const override {
        DisplayMode mode;
        if (displayIndex != 0 || app_ == nullptr || app_->window == nullptr) {
            return mode;
        }
        mode.width = ANativeWindow_getWidth(app_->window);
        mode.height = ANativeWindow_getHeight(app_->window);
        return mode;
    }

    std::vector<DisplayMode> modes(int displayIndex) const override {
        std::vector<DisplayMode> result;
        const DisplayMode mode = currentMode(displayIndex);
        if (mode.width > 0 && mode.height > 0) {
            result.push_back(mode);
        }
        return result;
    }

    bool supportsExclusiveFullscreen() const override { return false; }

private:
    android_app* app_ = nullptr;
};

// ---------------------------------------------------------------------------
// URL opener
// ---------------------------------------------------------------------------

class AndroidUrlOpener final : public IUrlOpener {
public:
    explicit AndroidUrlOpener(android_app* app) : app_(app) {}

    bool open(const std::string& url) override {
        if (url.empty() || app_ == nullptr || app_->activity == nullptr) {
            return false;
        }
        JavaVM* vm = app_->activity->vm;
        jobject activity = app_->activity->clazz;
        if (vm == nullptr || activity == nullptr) {
            return false;
        }

        JNIEnv* env = nullptr;
        const bool attached =
            vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK;
        if (attached && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return false;
        }

        bool ok = false;
        jclass uriClass = env->FindClass("android/net/Uri");
        jclass intentClass = nullptr;
        jstring action = nullptr;
        do {
            if (uriClass == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            jmethodID parse = env->GetStaticMethodID(
                uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
            if (parse == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }
            jstring urlStr = env->NewStringUTF(url.c_str());
            jobject uri = env->CallStaticObjectMethod(uriClass, parse, urlStr);
            env->DeleteLocalRef(urlStr);
            if (uri == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }

            intentClass = env->FindClass("android/content/Intent");
            if (intentClass == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(uri);
                break;
            }
            jfieldID actionView =
                env->GetStaticFieldID(intentClass, "ACTION_VIEW", "Ljava/lang/String;");
            if (actionView == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(uri);
                break;
            }
            action = static_cast<jstring>(env->GetStaticObjectField(intentClass, actionView));
            jmethodID init =
                env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;Landroid/net/Uri;)V");
            if (init == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(uri);
                break;
            }
            jobject intent = env->NewObject(intentClass, init, action, uri);
            env->DeleteLocalRef(uri);
            if (intent == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                break;
            }

            jclass activityClass = env->GetObjectClass(activity);
            jmethodID startActivity =
                env->GetMethodID(activityClass, "startActivity", "(Landroid/content/Intent;)V");
            if (startActivity == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                env->DeleteLocalRef(activityClass);
                env->DeleteLocalRef(intent);
                break;
            }
            env->CallVoidMethod(activity, startActivity, intent);
            ok = !env->ExceptionCheck();
            if (!ok) {
                env->ExceptionClear();
            }
            env->DeleteLocalRef(activityClass);
            env->DeleteLocalRef(intent);
        } while (false);

        if (action != nullptr) {
            env->DeleteLocalRef(action);
        }
        if (intentClass != nullptr) {
            env->DeleteLocalRef(intentClass);
        }
        if (uriClass != nullptr) {
            env->DeleteLocalRef(uriClass);
        }
        if (attached) {
            vm->DetachCurrentThread();
        }
        return ok;
    }

private:
    android_app* app_ = nullptr;
};

// ---------------------------------------------------------------------------
// Platform
// ---------------------------------------------------------------------------

class AndroidPlatform final : public IPlatform {
public:
    explicit AndroidPlatform(PlatformInit init)
        : init_(std::move(init)),
          log_(init_.appName),
          identity_{init_.appName, init_.organization, {}} {
        app_ = static_cast<android_app*>(init_.nativeApp);
    }

    ~AndroidPlatform() override {
        if (app_ != nullptr) {
            app_->onAppCmd = nullptr;
            app_->onInputEvent = nullptr;
            app_->userData = nullptr;
        }
        if (currentPlatform() == this) {
            setCurrentPlatform(nullptr);
        }
    }

    bool initialize(std::string* error) {
        if (app_ == nullptr) {
            if (error != nullptr) {
                *error = "Android platform requires android_app in PlatformInit::nativeApp";
            }
            return false;
        }

        app_->userData = this;
        app_->onAppCmd = &AndroidPlatform::handleAppCmd;
        app_->onInputEvent = &AndroidPlatform::handleInput;

        if (app_->activity != nullptr) {
            if (app_->activity->assetManager != nullptr) {
                d2::SetAndroidAssetManager(app_->activity->assetManager);
                assetManager_ = app_->activity->assetManager;
            }
            if (app_->activity->internalDataPath != nullptr) {
                internalDataPath_ = app_->activity->internalDataPath;
            }
        }

        // Wait for the first window before the game creates GL resources;
        // creating the renderer with no surface is the most common Android
        // startup crash.
        while (app_->window == nullptr && app_->destroyRequested == 0) {
            int events = 0;
            android_poll_source* source = nullptr;
            if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
                if (source != nullptr) {
                    source->process(app_, source);
                }
            }
        }
        if (app_->destroyRequested != 0) {
            if (error != nullptr) {
                *error = "Activity was destroyed before a window became available";
            }
            return false;
        }
        return true;
    }

    PlatformKind kind() const override { return PlatformKind::Android; }
    const PlatformInit& init() const override { return init_; }
    IClock& clock() override { return clock_; }
    ILogSink& log() override { return log_; }

    std::unique_ptr<IWindow> createWindow(const WindowDesc& desc, std::string* error) override {
        if (app_ == nullptr) {
            if (error != nullptr) {
                *error = "Android platform has no android_app";
            }
            return nullptr;
        }
        auto window = std::make_unique<AndroidWindowBackend>(app_, desc);
        if (!window->valid()) {
            if (error != nullptr) {
                *error = "Failed to create the Android window";
            }
            return nullptr;
        }
        window_ = window.get();
        return window;
    }

    std::unique_ptr<IAssetSource> createAssetSource(
        const AssetSourceDesc&, std::string* error) override {
        if (assetManager_ == nullptr) {
            if (error != nullptr) {
                *error = "No APK asset manager available";
            }
            return nullptr;
        }
        return std::make_unique<AndroidAssetSource>(assetManager_);
    }

    std::unique_ptr<IPathProvider> createPathProvider() override {
        return std::make_unique<StdPathProvider>(identity_, internalDataPath_);
    }

    std::unique_ptr<IDisplayEnumerator> createDisplayEnumerator() override {
        return std::make_unique<AndroidDisplayEnumerator>(app_);
    }

    std::unique_ptr<IUrlOpener> createUrlOpener() override {
        return std::make_unique<AndroidUrlOpener>(app_);
    }

private:
    static void handleAppCmd(android_app* app, int32_t cmd) {
        auto* self = app != nullptr ? static_cast<AndroidPlatform*>(app->userData) : nullptr;
        if (self == nullptr || self->window_ == nullptr) {
            return;
        }
        switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            self->window_->onWindowCreated();
            break;
        case APP_CMD_TERM_WINDOW:
            self->window_->onWindowDestroyed();
            break;
        case APP_CMD_GAINED_FOCUS:
            self->window_->onGainedFocus();
            break;
        case APP_CMD_LOST_FOCUS:
            self->window_->onLostFocus();
            break;
        case APP_CMD_PAUSE:
            self->window_->onPaused();
            break;
        case APP_CMD_RESUME:
            self->window_->onResumed();
            break;
        case APP_CMD_DESTROY:
            // shouldClose() already consults app->destroyRequested.
            break;
        default:
            break;
        }
    }

    static int32_t handleInput(android_app* app, AInputEvent* event) {
        auto* self = app != nullptr ? static_cast<AndroidPlatform*>(app->userData) : nullptr;
        if (self == nullptr || self->window_ == nullptr || event == nullptr) {
            return 0;
        }
        return self->processInput(event);
    }

    int32_t processInput(AInputEvent* event) {
        const int32_t type = AInputEvent_getType(event);
        if (type == AINPUT_EVENT_TYPE_MOTION) {
            const int32_t action = AMotionEvent_getAction(event);
            const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
            const size_t count = AMotionEvent_getPointerCount(event);
            if (count == 0) {
                return 0;
            }
            // Forward every finger so the virtual gamepad and the tap path can
            // split touches; the window keeps the primary cursor in sync for
            // legacy single-touch consumers.
            const int32_t actionIndex =
                (masked == AMOTION_EVENT_ACTION_POINTER_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_UP)
                ? ((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                   >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)
                : 0;
            for (size_t index = 0; index < count; ++index) {
                const int32_t androidId = AMotionEvent_getPointerId(event, index);
                const float x = AMotionEvent_getX(event, index);
                const float y = AMotionEvent_getY(event, index);
                const bool isActionPointer = static_cast<int32_t>(index) == actionIndex;
                if (masked == AMOTION_EVENT_ACTION_MOVE) {
                    window_->onMotionPointer(androidId, x, y, true, false, false);
                } else if (masked == AMOTION_EVENT_ACTION_DOWN
                    || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                    window_->onMotionPointer(androidId, x, y, true, isActionPointer, false);
                } else if (masked == AMOTION_EVENT_ACTION_UP
                    || masked == AMOTION_EVENT_ACTION_POINTER_UP
                    || masked == AMOTION_EVENT_ACTION_CANCEL) {
                    // UP/CANCEL lifts every tracked finger; POINTER_UP lifts one.
                    const bool liftAll =
                        masked == AMOTION_EVENT_ACTION_UP
                        || masked == AMOTION_EVENT_ACTION_CANCEL;
                    window_->onMotionPointer(
                        androidId, x, y, false, false, liftAll || isActionPointer);
                }
            }
            // Pinch is not turned into a wheel here: the raw contact table is
            // forwarded, and the game decides which fingers are free of the
            // virtual pad. Translating every two-finger move into zoom made a
            // player steering with the left thumb and holding a button with
            // the right one zoom the camera on every frame.
            return 1;
        }
        if (type == AINPUT_EVENT_TYPE_KEY) {
            const int32_t keyCode = AKeyEvent_getKeyCode(event);
            const int32_t action = AKeyEvent_getAction(event);
            const int rwKey = toPalKey(keyCode);
            if (rwKey == key::Unknown) {
                return 0;
            }
            window_->onKey(rwKey, action == AKEY_EVENT_ACTION_DOWN);
            // BACK is consumed as Cancel so the game can show its own pause
            // menu instead of the OS killing the activity immediately.
            return 1;
        }
        return 0;
    }

    PlatformInit init_;
    StdLogSink log_;
    StdClock clock_;
    AppIdentity identity_;
    android_app* app_ = nullptr;
    AAssetManager* assetManager_ = nullptr;
    std::string internalDataPath_;
    AndroidWindowBackend* window_ = nullptr;
};

} // namespace

std::unique_ptr<IPlatform> createAndroidPlatform(const PlatformInit& init, std::string* error) {
    auto platform = std::make_unique<AndroidPlatform>(init);
    if (!platform->initialize(error)) {
        return nullptr;
    }
    return platform;
}

} // namespace uniscope::pal

#endif // __ANDROID__
