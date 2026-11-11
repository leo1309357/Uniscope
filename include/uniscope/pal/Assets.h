/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_PAL_ASSETS_H
#define UNISCOPE_PAL_ASSETS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uniscope::pal {

struct AssetSourceDesc {
    // 桌面：资源根目录；Android：""（走 APK assets）；iOS：bundle 根。
    std::string rootPath;
    bool allowEmbeddedFallback = true;
};

class IAssetStream {
public:
    virtual ~IAssetStream() = default;
    virtual bool read(void* dst, size_t bytes) = 0;
    virtual bool seek(size_t offset) = 0;
    virtual size_t size() const = 0;
};

class IAssetSource {
public:
    virtual ~IAssetSource() = default;

    // 逻辑路径使用 '/' 分隔，相对资源根，例如 "shaders/rect2d.vert"。
    virtual bool exists(const std::string& logicalPath) const = 0;
    virtual bool readText(const std::string& logicalPath, std::string* out) const = 0;
    virtual bool readBytes(const std::string& logicalPath, std::vector<uint8_t>* out) const = 0;

    // 需要随机访问/流式读取时使用；不支持则返回 nullptr。
    virtual std::unique_ptr<IAssetStream> openStream(const std::string& logicalPath) const = 0;

    // 列出目录下的直接子项名（不含父路径）。目录不存在/不支持时返回 false。
    // 用于 APK assets 这类没有真实目录的源：调用方据此发现资源，再逐项读取。
    virtual bool listDirectory(
        const std::string& logicalPath,
        std::vector<std::string>* out) const = 0;

    // 仅当资源能映射为真实 OS 路径时返回 true（桌面常见，Android 通常 false）。
    virtual bool resolveToOsPath(const std::string& logicalPath, std::string* out) const = 0;

    // 诊断用途："filesystem" / "apk" / "bundle"。
    virtual const char* kindName() const = 0;
};

} // namespace uniscope::pal

#endif
