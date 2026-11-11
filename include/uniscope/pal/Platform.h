/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_PLATFORM_H
#define UNISCOPE_PAL_PLATFORM_H

#include <memory>
#include <string>

namespace uniscope::pal {

enum class PlatformKind { Windows, Linux, MacOS, Android, IOS, Web, Unknown };
enum class GfxApi { OpenGL33, OpenGLES30, Metal, WebGL2, Unknown };
enum class LogLevel { Trace, Debug, Info, Warning, Error };

struct PlatformInit {
    void* nativeApp = nullptr;   // Android: android_app*; iOS: UIApplication*
    int argc = 0;
    char** argv = nullptr;
    std::string appName = "App";      //日志 tag / 用户数据目录
    std::string organization = "Org"; // macOS/Linux 目录约定
};

struct GfxCapabilities {
    GfxApi api = GfxApi::Unknown;
    int maxMsaaSamples = 0;
    float maxAnisotropy = 1.0f;
    std::string vendor;
    std::string renderer;
    std::string version;
};

class IClock;
class ILogSink;
class IWindow;
class IAssetSource;
class IPathProvider;
class IDisplayEnumerator;
class IUrlOpener;

struct WindowDesc;      // Window.h
struct AssetSourceDesc; // Assets.h

class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual PlatformKind kind() const = 0;
    virtual const PlatformInit& init() const = 0;

    virtual IClock& clock() = 0;
    virtual ILogSink& log() = 0;

    // 失败返回 nullptr，并把原因写入 error（可为 nullptr）。
    virtual std::unique_ptr<IWindow> createWindow(const WindowDesc& desc, std::string* error) = 0;
    virtual std::unique_ptr<IAssetSource> createAssetSource(const AssetSourceDesc& desc,
                                                            std::string* error) = 0;
    virtual std::unique_ptr<IPathProvider> createPathProvider() = 0;
    virtual std::unique_ptr<IDisplayEnumerator> createDisplayEnumerator() = 0;
    virtual std::unique_ptr<IUrlOpener> createUrlOpener() = 0;
};

// 每个平台只实现这一个符号；PlatformFactory.cpp 由构建系统按后端选择编译。
std::unique_ptr<IPlatform> createPlatform(const PlatformInit& init, std::string* error);

// 进程内唯一的活动平台。供薄包装（ResourcePaths/UserDataPaths 等）与引擎
// 内部默认资源源使用。createPlatform 成功后自动登记。
IPlatform* currentPlatform();
void setCurrentPlatform(IPlatform* platform);

} // namespace uniscope::pal

#endif
