/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_WINDOW_H
#define UNISCOPE_PAL_WINDOW_H

#include "Input.h"
#include "Platform.h"

#include <string>

namespace uniscope::pal {

enum class WindowMode { Windowed, Borderless, Fullscreen };

enum class LifecycleState {
    Active,      // 前台、可渲染
    Paused,      // 后台（Android onPause / 桌面最小化）
    SurfaceLost, // 图形 surface 不可用，必须释放并等待重建
    Terminated   // 不可恢复，应退出主循环
};

struct LifecycleEvent {
    LifecycleState state = LifecycleState::Active;
    bool isRecovery = false;
};

struct WindowCapabilities {
    bool supportsResize = false;
    bool supportsBorderless = false;
    bool supportsExclusiveFullscreen = false;
    bool supportsVsyncToggle = false;
    bool supportsMultipleDisplays = false;
    bool supportsTouch = false;
    bool supportsPointerCapture = false;
    bool supportsIme = false;
    bool supportsSuspendResume = false;
    int maxMsaaSamples = 0;
};

struct WindowDesc {
    int width = 1280;
    int height = 720;
    std::string title;
    int msaaSamples = 0;
    bool visible = true;
    bool resizable = true;
    WindowMode mode = WindowMode::Windowed;
    // 可选的窗口图标（OS 路径）；空表示不设置。
    std::string iconPath;
};

// 文本输入：IME 组合态。没有 IME 的后端返回 nullptr。
class ITextInput {
public:
    virtual ~ITextInput() = default;
    virtual std::string consumeCommittedText() = 0;
    virtual const std::string* composition() const = 0;
    virtual void setCursorRect(int x, int y, int w, int h) = 0;
};

class IWindow {
public:
    virtual ~IWindow() = default;

    virtual bool valid() const = 0;
    virtual WindowCapabilities capabilities() const = 0;

    // ---- 事件泵 ----
    virtual void pollEvents() = 0;
    virtual void waitEventsTimeout(double seconds) = 0;
    virtual bool shouldClose() const = 0;
    virtual void requestClose() = 0;

    // ---- 每帧输入 ----
    virtual void beginInputFrame() = 0;
    virtual void clearFrameEdges() = 0;
    virtual bool isKeyPressed(int key) const = 0;
    virtual const PointerState& pointer() const = 0;
    virtual int touchContactCount() const = 0;
    virtual const TouchContact* touchContacts() const = 0;
    // 兼容既有调用：把当前触点写入 out，返回数量。
    virtual int touchContacts(TouchContact* out, int max) const = 0;

    // ---- 文本 / 键位捕获 ----
    virtual ITextInput* textInput() = 0;
    virtual std::string consumeTypedText() = 0;
    virtual bool consumeBackspace() = 0;
    virtual void setKeyCaptureEnabled(bool enabled) = 0;
    virtual int consumeCapturedKey() = 0;

    // ---- 尺寸与状态 ----
    virtual int framebufferWidth() const = 0;
    virtual int framebufferHeight() const = 0;
    virtual float framebufferAspect() const = 0;
    virtual float displayDensityDpi() const = 0;
    virtual bool isFocused() const = 0;
    virtual bool isIconified() const = 0;
    virtual bool shouldSuspendRendering() const = 0;

    // ---- 生命周期 ----
    virtual LifecycleState lifecycle() const = 0;
    virtual bool pollLifecycleEvent(LifecycleEvent* out) = 0;

    // ---- 显示模式 / 上下文 ----
    virtual void setTitle(const std::string& title) = 0;
    virtual bool setMode(WindowMode mode, int width, int height, int refreshRate) = 0;
    virtual bool setVsync(bool enabled) = 0;
    virtual void makeContextCurrent() = 0;
    virtual void swapBuffers() = 0;
    virtual double time() const = 0;

    virtual GfxApi gfxApi() const = 0;
    virtual GfxCapabilities queryGfxCapabilities() = 0;

    // 平台原生句柄：Android = ANativeWindow*，桌面 = GLFWwindow*；引擎内部使用。
    virtual void* nativeHandle() = 0;
};

} // namespace uniscope::pal

#endif
