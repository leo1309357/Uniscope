/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "Texture2D.h"

#include "AndroidAssetReader.h"
#include "ParallelFor.h"
#include "TextureFilter.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace uniscope::d2 {

namespace {

bool channelsSupported(int channels) {
    return channels == 1 || channels == 2 || channels == 4;
}

bool pixelsUsable(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    return width > 0 && height > 0 && channelsSupported(channels)
        && static_cast<long long>(pixels.size())
            >= static_cast<long long>(width) * height * channels;
}

GLenum internalFormatFor(int channels) {
    switch (channels) {
    case 1: return GL_R8;
    case 2: return GL_RG8;
    default: return GL_RGBA8;
    }
}

GLenum pixelFormatFor(int channels) {
    switch (channels) {
    case 1: return GL_RED;
    case 2: return GL_RG;
    default: return GL_RGBA;
    }
}

// Rows of one- and two-channel images are not multiples of four bytes in
// general, and the default unpack alignment of 4 would make GL read them with
// the wrong stride. Set once around every upload, restored after.
class UnpackAlignmentScope {
public:
    UnpackAlignmentScope() {
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }
    ~UnpackAlignmentScope() {
        glPixelStorei(GL_UNPACK_ALIGNMENT, previous_);
    }
    UnpackAlignmentScope(const UnpackAlignmentScope&) = delete;
    UnpackAlignmentScope& operator=(const UnpackAlignmentScope&) = delete;

private:
    GLint previous_ = 4;
};

unsigned char* loadImageRgba(const std::string& utf8Path, int& width, int& height, int& channels) {
#if defined(_WIN32)
    const std::filesystem::path path = std::filesystem::u8path(utf8Path);
    std::FILE* file = _wfopen(path.c_str(), L"rb");
#else
    std::FILE* file = std::fopen(utf8Path.c_str(), "rb");
#endif
    if (file != nullptr) {
        unsigned char* pixels = stbi_load_from_file(file, &width, &height, &channels, 4);
        std::fclose(file);
        if (pixels != nullptr) {
            return pixels;
        }
    }
#if defined(__ANDROID__)
    // APK assets are not files on disk; decode from the asset buffer.
    const std::vector<unsigned char> bytes = AndroidAssetReadBytes(utf8Path);
    if (!bytes.empty()) {
        return stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    }
#endif
    return nullptr;
}

// Reduces a `sourceWidth` x `sourceHeight` block of texels to half size with a
// 2x2 box filter, writing `targetWidth` x `targetHeight` texels. Strides are in
// texels, so a block may be one cell inside a larger level image. Odd source
// sizes clamp the footprint to the last row or column rather than reading past
// the block, which is what keeps a cell's reduction inside its own cell.
//
// Four-channel data is reduced alpha-weighted: each texel's colour counts in
// proportion to its coverage, so the transparent black around a cutout prop no
// longer darkens the prop's edge in every minified level. Where all four texels
// are transparent the plain mean is kept, so a fully clear area stays whatever
// colour it was painted. Integer throughout; the old float lambda per channel
// made the chain for a 23 MB atlas a visible stall on every quality change.
void downsampleBlock(
    const unsigned char* source,
    std::size_t sourceStride,
    int sourceWidth,
    int sourceHeight,
    unsigned char* target,
    std::size_t targetStride,
    int targetWidth,
    int targetHeight,
    int channels) {
    for (int y = 0; y < targetHeight; ++y) {
        const int sy0 = std::min(y * 2, sourceHeight - 1);
        const int sy1 = std::min(y * 2 + 1, sourceHeight - 1);
        const unsigned char* row0 = source + static_cast<std::size_t>(sy0) * sourceStride * channels;
        const unsigned char* row1 = source + static_cast<std::size_t>(sy1) * sourceStride * channels;
        unsigned char* out = target + static_cast<std::size_t>(y) * targetStride * channels;
        for (int x = 0; x < targetWidth; ++x) {
            const int sx0 = std::min(x * 2, sourceWidth - 1);
            const int sx1 = std::min(x * 2 + 1, sourceWidth - 1);
            const unsigned char* t00 = row0 + static_cast<std::size_t>(sx0) * channels;
            const unsigned char* t10 = row0 + static_cast<std::size_t>(sx1) * channels;
            const unsigned char* t01 = row1 + static_cast<std::size_t>(sx0) * channels;
            const unsigned char* t11 = row1 + static_cast<std::size_t>(sx1) * channels;
            unsigned char* dst = out + static_cast<std::size_t>(x) * channels;

            if (channels == 4) {
                const unsigned a00 = t00[3];
                const unsigned a10 = t10[3];
                const unsigned a01 = t01[3];
                const unsigned a11 = t11[3];
                const unsigned alphaSum = a00 + a10 + a01 + a11;
                if (alphaSum == 0) {
                    for (int channel = 0; channel < 3; ++channel) {
                        dst[channel] = static_cast<unsigned char>(
                            (t00[channel] + t10[channel] + t01[channel] + t11[channel] + 2u) / 4u);
                    }
                    dst[3] = 0;
                } else {
                    for (int channel = 0; channel < 3; ++channel) {
                        const unsigned weighted = t00[channel] * a00 + t10[channel] * a10
                            + t01[channel] * a01 + t11[channel] * a11;
                        dst[channel] = static_cast<unsigned char>((weighted + alphaSum / 2) / alphaSum);
                    }
                    dst[3] = static_cast<unsigned char>((alphaSum + 2u) / 4u);
                }
            } else {
                for (int channel = 0; channel < channels; ++channel) {
                    dst[channel] = static_cast<unsigned char>(
                        (t00[channel] + t10[channel] + t01[channel] + t11[channel] + 2u) / 4u);
                }
            }
        }
    }
}

} // namespace

Texture2D::~Texture2D() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : textureId_(other.textureId_),
      width_(other.width_),
      height_(other.height_),
      hasMipmaps_(other.hasMipmaps_) {
    other.textureId_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.hasMipmaps_ = false;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        if (textureId_ != 0) {
            glDeleteTextures(1, &textureId_);
        }

        textureId_ = other.textureId_;
        width_ = other.width_;
        height_ = other.height_;
        hasMipmaps_ = other.hasMipmaps_;
        other.textureId_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.hasMipmaps_ = false;
    }
    return *this;
}

float Texture2D::aspect() const {
    if (height_ <= 0) {
        return 1.0f;
    }
    return static_cast<float>(width_) / static_cast<float>(height_);
}

void Texture2D::applyFilter(uniscope::d2::TextureQuality quality) const {
    applyTextureFilter(textureId_, quality, hasMipmaps_);
}

void Texture2D::updateRegion(
    const std::vector<unsigned char>& rgbaPixels,
    int imageWidth,
    int x,
    int y,
    int width,
    int height) const {
    if (textureId_ == 0 || imageWidth <= 0 || width <= 0 || height <= 0 || x < 0 || y < 0) {
        return;
    }
    const std::size_t imageHeight =
        rgbaPixels.size() / (static_cast<std::size_t>(imageWidth) * 4);
    if (static_cast<std::size_t>(x + width) > static_cast<std::size_t>(imageWidth)
        || static_cast<std::size_t>(y + height) > imageHeight) {
        return;
    }
    // glTexSubImage2D reads tightly packed rows, but the region's rows are
    // strided inside the full image, so gather it first.
    std::vector<unsigned char> region(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    for (int row = 0; row < height; ++row) {
        const unsigned char* source = rgbaPixels.data()
            + (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(imageWidth)
               + static_cast<std::size_t>(x))
                * 4;
        std::memcpy(
            region.data() + static_cast<std::size_t>(row) * rowBytes, source, rowBytes);
    }
    glBindTexture(GL_TEXTURE_2D, textureId_);
    {
        UnpackAlignmentScope alignment;
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, region.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::refreshCellMipmaps(
    const std::vector<unsigned char>& rgbaPixels,
    int imageWidth,
    int cellsPerRow,
    int cellIndex) const {
    if (textureId_ == 0 || !hasMipmaps_ || cellsPerRow <= 0 || imageWidth <= 0 || cellIndex < 0
        || cellIndex >= cellsPerRow * cellsPerRow) {
        return;
    }
    const std::size_t imageHeight = rgbaPixels.size() / (static_cast<std::size_t>(imageWidth) * 4);
    if (imageHeight == 0 || imageWidth % cellsPerRow != 0
        || imageHeight % static_cast<std::size_t>(cellsPerRow) != 0) {
        return;
    }

    const int cellX = cellIndex % cellsPerRow;
    const int cellY = cellIndex / cellsPerRow;
    int cellWidth = imageWidth / cellsPerRow;
    int cellHeight = static_cast<int>(imageHeight) / cellsPerRow;

    // Copy the touched cell's level-0 pixels out, then rebuild only this cell's
    // per-cell mip levels from them, mirroring loadCellPreservingFromPixels.
    // Per-cell levels stop once the cell is 1 texel; the handful of whole-image
    // tail levels (sampled only at extreme minification) keep their contents.
    std::vector<unsigned char> level(
        static_cast<std::size_t>(cellWidth) * static_cast<std::size_t>(cellHeight) * 4);
    const std::size_t cellRowBytes = static_cast<std::size_t>(cellWidth) * 4;
    for (int row = 0; row < cellHeight; ++row) {
        const unsigned char* source = rgbaPixels.data()
            + (static_cast<std::size_t>(cellY * cellHeight + row)
                   * static_cast<std::size_t>(imageWidth)
               + static_cast<std::size_t>(cellX * cellWidth))
                * 4;
        std::memcpy(level.data() + static_cast<std::size_t>(row) * cellRowBytes, source, cellRowBytes);
    }

    glBindTexture(GL_TEXTURE_2D, textureId_);
    UnpackAlignmentScope alignment;

    int levelIndex = 0;
    while (cellWidth > 1 && cellHeight > 1) {
        const int nextCellWidth = std::max(1, cellWidth / 2);
        const int nextCellHeight = std::max(1, cellHeight / 2);
        std::vector<unsigned char> next(
            static_cast<std::size_t>(nextCellWidth) * static_cast<std::size_t>(nextCellHeight) * 4);
        downsampleBlock(
            level.data(),
            static_cast<std::size_t>(cellWidth),
            cellWidth,
            cellHeight,
            next.data(),
            static_cast<std::size_t>(nextCellWidth),
            nextCellWidth,
            nextCellHeight,
            4);

        ++levelIndex;
        glTexSubImage2D(
            GL_TEXTURE_2D,
            levelIndex,
            cellX * nextCellWidth,
            cellY * nextCellHeight,
            nextCellWidth,
            nextCellHeight,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            next.data());
        level.swap(next);
        cellWidth = nextCellWidth;
        cellHeight = nextCellHeight;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D Texture2D::uploadPixels(
    const unsigned char* pixels,
    int width,
    int height,
    int channels,
    uniscope::d2::TextureQuality quality,
    bool flipVertical,
    TextureWrap2D wrap) {
    Texture2D texture;
    if (pixels == nullptr || width <= 0 || height <= 0 || !channelsSupported(channels)) {
        return texture;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    const unsigned char* uploadPixels = pixels;
    std::vector<unsigned char> flipped;
    if (flipVertical) {
        flipped.resize(rowBytes * static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            const int srcY = height - 1 - y;
            std::copy(
                pixels + static_cast<std::size_t>(srcY) * rowBytes,
                pixels + static_cast<std::size_t>(srcY + 1) * rowBytes,
                flipped.begin() + static_cast<std::size_t>(y) * rowBytes);
        }
        uploadPixels = flipped.data();
    }

    const GLint wrapMode = wrap == TextureWrap2D::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;

    glGenTextures(1, &texture.textureId_);
    glBindTexture(GL_TEXTURE_2D, texture.textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    {
        UnpackAlignmentScope alignment;
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            static_cast<GLint>(internalFormatFor(channels)),
            width,
            height,
            0,
            pixelFormatFor(channels),
            GL_UNSIGNED_BYTE,
            uploadPixels);
    }

    // Every tier gets a mip chain. Minification without one aliases on any
    // texture smaller on screen than in memory - which is most of a world at
    // most camera distances - and the chain costs a third more memory, which
    // is the cheapest aliasing fix a GPU offers. The tier decides how the
    // chain is filtered (see TextureFilter.h), not whether it exists.
    glGenerateMipmap(GL_TEXTURE_2D);
    texture.hasMipmaps_ = true;

    applyTextureFilter(texture.textureId_, quality, true);
    glBindTexture(GL_TEXTURE_2D, 0);

    texture.width_ = width;
    texture.height_ = height;
    return texture;
}

Texture2D Texture2D::loadNearestFromPixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels) {
    if (!pixelsUsable(pixels, width, height, channels)) {
        return {};
    }

    Texture2D texture;
    glGenTextures(1, &texture.textureId_);
    glBindTexture(GL_TEXTURE_2D, texture.textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // The contract: nearest on both axes and no levels above 0. The complete
    // chain of a normal upload is what a lookup buffer must NOT have.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    {
        UnpackAlignmentScope alignment;
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            static_cast<GLint>(internalFormatFor(channels)),
            width,
            height,
            0,
            pixelFormatFor(channels),
            GL_UNSIGNED_BYTE,
            pixels.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    texture.hasMipmaps_ = false;
    texture.width_ = width;
    texture.height_ = height;
    return texture;
}

Texture2D Texture2D::loadTilingFromPixels(
    const std::vector<unsigned char>& rgbaPixels,
    int width,
    int height) {
    if (!pixelsUsable(rgbaPixels, width, height, 4)) {
        return {};
    }
    return uploadPixels(
        rgbaPixels.data(),
        width,
        height,
        4,
        uniscope::d2::TextureQuality::Medium,
        false,
        TextureWrap2D::Repeat);
}

std::vector<unsigned char> Texture2D::loadRawRgba(const std::string& path) {
    return loadRawRgbaWithSize(path).pixels;
}

Texture2D::RawRgbaImage Texture2D::loadRawRgbaWithSize(const std::string& path) {
    RawRgbaImage image;
    int channels = 0;
    unsigned char* pixels = loadImageRgba(path, image.width, image.height, channels);
    if (pixels == nullptr || image.width <= 0 || image.height <= 0) {
        image.width = 0;
        image.height = 0;
        return image;
    }
    image.pixels.assign(
        pixels,
        pixels + static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);
    stbi_image_free(pixels);
    return image;
}

Texture2D Texture2D::loadCellPreservingFromPixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int cellsPerRow,
    uniscope::d2::TextureQuality quality,
    int cellsPerColumn,
    int channels) {
    const int cellsTall = cellsPerColumn > 0 ? cellsPerColumn : cellsPerRow;
    if (!pixelsUsable(pixels, width, height, channels) || cellsPerRow <= 0
        || cellsTall <= 0 || width % cellsPerRow != 0 || height % cellsTall != 0) {
        return {};
    }

    // Build the whole mip chain on the CPU. Cell levels are halved per cell so
    // minification never averages neighbouring frames together (that is what
    // turns a tile grid into a visible dark lattice as the camera pulls back).
    // The final whole-image levels still have to exist: OpenGL treats a texture
    // whose chain stops above 1x1 as incomplete, and an incomplete texture
    // samples as solid black.
    Texture2D texture;
    glGenTextures(1, &texture.textureId_);
    glBindTexture(GL_TEXTURE_2D, texture.textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    UnpackAlignmentScope alignment;

    const GLint internalFormat = static_cast<GLint>(internalFormatFor(channels));
    const GLenum pixelFormat = pixelFormatFor(channels);
    const std::size_t texelBytes = static_cast<std::size_t>(channels);

    // Level 0 uploads straight from the caller's buffer; only the reduced
    // levels need storage of their own, and the two scratch buffers are
    // swapped rather than reallocated per level.
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, pixelFormat, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<unsigned char> level;
    std::vector<unsigned char> next;
    const unsigned char* levelData = pixels.data();
    int levelWidth = width;
    int levelHeight = height;
    int levelIndex = 0;
    int cellWidth = width / cellsPerRow;
    int cellHeight = height / cellsTall;

    while (levelWidth > 1 || levelHeight > 1) {
        ++levelIndex;
        if (cellWidth > 1 && cellHeight > 1) {
            const int nextCellWidth = std::max(1, cellWidth / 2);
            const int nextCellHeight = std::max(1, cellHeight / 2);
            const int nextWidth = nextCellWidth * cellsPerRow;
            const int nextHeight = nextCellHeight * cellsTall;
            next.resize(static_cast<std::size_t>(nextWidth) * static_cast<std::size_t>(nextHeight) * texelBytes);

            // One task per cell: each writes its own rectangle of `next`, so
            // the level reduces across every core instead of one.
            const std::size_t cellCount =
                static_cast<std::size_t>(cellsPerRow) * static_cast<std::size_t>(cellsTall);
            unsigned char* nextData = next.data();
            parallelFor(cellCount, [&, nextData](std::size_t cellIndex) {
                const int cellX = static_cast<int>(cellIndex % static_cast<std::size_t>(cellsPerRow));
                const int cellY = static_cast<int>(cellIndex / static_cast<std::size_t>(cellsPerRow));
                const unsigned char* source = levelData
                    + (static_cast<std::size_t>(cellY) * cellHeight * levelWidth
                       + static_cast<std::size_t>(cellX) * cellWidth)
                        * texelBytes;
                unsigned char* target = nextData
                    + (static_cast<std::size_t>(cellY) * nextCellHeight * nextWidth
                       + static_cast<std::size_t>(cellX) * nextCellWidth)
                        * texelBytes;
                downsampleBlock(
                    source,
                    static_cast<std::size_t>(levelWidth),
                    cellWidth,
                    cellHeight,
                    target,
                    static_cast<std::size_t>(nextWidth),
                    nextCellWidth,
                    nextCellHeight,
                    channels);
            });

            levelWidth = nextWidth;
            levelHeight = nextHeight;
            cellWidth = nextCellWidth;
            cellHeight = nextCellHeight;
        } else {
            // Cells are single texels now: the tail reduces the whole image.
            const int nextWidth = std::max(1, levelWidth / 2);
            const int nextHeight = std::max(1, levelHeight / 2);
            next.resize(static_cast<std::size_t>(nextWidth) * static_cast<std::size_t>(nextHeight) * texelBytes);
            downsampleBlock(
                levelData,
                static_cast<std::size_t>(levelWidth),
                levelWidth,
                levelHeight,
                next.data(),
                static_cast<std::size_t>(nextWidth),
                nextWidth,
                nextHeight,
                channels);
            levelWidth = nextWidth;
            levelHeight = nextHeight;
        }

        glTexImage2D(
            GL_TEXTURE_2D,
            levelIndex,
            internalFormat,
            levelWidth,
            levelHeight,
            0,
            pixelFormat,
            GL_UNSIGNED_BYTE,
            next.data());
        level.swap(next);
        levelData = level.data();
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelIndex);
    texture.hasMipmaps_ = true;
    applyTextureFilter(texture.textureId_, quality, true);
    glBindTexture(GL_TEXTURE_2D, 0);
    texture.width_ = width;
    texture.height_ = height;
    return texture;
}

Texture2D Texture2D::loadFromFile(const std::string& path, uniscope::d2::TextureQuality quality, bool flipVertical) {
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = loadImageRgba(path, width, height, channels);
    if (pixels == nullptr) {
        return {};
    }

    Texture2D texture = uploadPixels(pixels, width, height, 4, quality, flipVertical);
    stbi_image_free(pixels);
    return texture;
}

Texture2D Texture2D::loadFromPixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    uniscope::d2::TextureQuality quality,
    int channels) {
    if (!pixelsUsable(pixels, width, height, channels)) {
        return {};
    }
    return uploadPixels(pixels.data(), width, height, channels, quality);
}

} // namespace uniscope::d2
