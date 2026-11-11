/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_TEXTURE2D_H
#define UNISCOPE_D2_GFX_TEXTURE2D_H

#include "uniscope/d2/Types.h"

#include "GlCompat.h"

#include <string>
#include <type_traits>
#include <vector>

namespace uniscope::d2 {

// Wrap mode for an uploaded texture. Repeat is required by textures sampled
// with wrapped detail-map coordinates (see DetailMap2D).
enum class TextureWrap2D {
    ClampToEdge,
    Repeat
};

class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    static Texture2D loadFromFile(
        const std::string& path,
        uniscope::d2::TextureQuality quality = uniscope::d2::TextureQuality::High,
        bool flipVertical = false);
    // `channels` is the number of bytes per texel in `pixels`: 4 uploads as
    // RGBA8, 2 as RG8 (sampled as (r, g, 0, 1)) and 1 as R8 (sampled as
    // (r, 0, 0, 1)). A single-channel field such as a signed distance font or
    // a two-channel one such as a tangent normal map costs a quarter or half
    // of the memory and upload time of the RGBA8 it used to be padded to, and
    // a shader that reads `.r` or `.rg` sees exactly the same values.
    static Texture2D loadFromPixels(
        const std::vector<unsigned char>& pixels,
        int width,
        int height,
        uniscope::d2::TextureQuality quality = uniscope::d2::TextureQuality::High,
        int channels = 4);

    // Uploads with GL_NEAREST magnification/minification and NO mip chain.
    //
    // For lookup buffers, not pictures: a material mask (design E1 5.2 /
    // invariant 21) stores an id per texel, and any filtering or mip averaging
    // invents a third id between two real ones. Every other upload in this file
    // deliberately builds a mip chain; this one is the documented exception and
    // exists so the rule is expressible instead of implicit in a caller's
    // choice of quality tier. `channels` follows loadFromPixels.
    static Texture2D loadNearestFromPixels(
        const std::vector<unsigned char>& pixels,
        int width,
        int height,
        int channels = 4);

    // Uploads with GL_REPEAT wrapping. Intended for seamlessly tiling textures
    // such as a character's ground shadow.
    static Texture2D loadTilingFromPixels(
        const std::vector<unsigned char>& rgbaPixels,
        int width,
        int height);

    // Decodes an image to RGBA8 without creating a GPU texture. Used to
    // composite user-supplied art into a procedurally generated atlas.
    static std::vector<unsigned char> loadRawRgba(const std::string& path);

    // Decoded RGBA8 image with its real dimensions. `loadRawRgba` drops the
    // size, so callers that need to map texels - compositing into an atlas
    // cell, for example - must use this instead of guessing the width from
    // the byte count (which yields the total texel count, not a row length).
    struct RawRgbaImage {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
    };
    static RawRgbaImage loadRawRgbaWithSize(const std::string& path);

    // Uploads an atlas whose pixels are laid out as a `cellsPerRow` x
    // `cellsPerRow` grid. Mip levels are generated per cell instead of across
    // the whole image, so minification never averages neighbouring frames
    // together. This is what keeps a tile atlas free of cross-cell seams.
    //
    // `cellsPerColumn` extends this to rectangular grids: -1 (the default)
    // keeps the square behaviour callers have always had, any other value
    // slices the height into that many cell rows instead of matching the
    // column count. A cell is still square in texels - `cellsPerColumn` is
    // only the number of grid rows, not a different cell size.
    //
    // The chain is built for every quality tier (the tier only picks the
    // filter), and four-channel levels are reduced alpha-weighted: a texel's
    // colour counts in proportion to its coverage, so a cutout prop's
    // transparent surround - black, in every generated atlas - no longer
    // darkens its edge as it minifies. `channels` follows loadFromPixels.
    static Texture2D loadCellPreservingFromPixels(
        const std::vector<unsigned char>& pixels,
        int width,
        int height,
        int cellsPerRow,
        uniscope::d2::TextureQuality quality = uniscope::d2::TextureQuality::High,
        int cellsPerColumn = -1,
        int channels = 4);

    // Re-applies the tier's sampler state. Whether a mip chain exists is a
    // property of the upload, remembered here, so a tier change can never ask
    // for mip filtering on a texture that has no levels (which samples black).
    void applyFilter(uniscope::d2::TextureQuality quality) const;

    // Re-uploads a sub-rectangle of this texture's level 0 in place. Unlike the
    // load* helpers it neither allocates a texture nor rebuilds mipmaps, so
    // refreshing one small cell of a large atlas costs a single
    // glTexSubImage2D instead of a whole-image upload chain. `rgbaPixels` is
    // the FULL image (`imageWidth` wide) and `x/y/width/height` select the
    // region within it. Higher mip levels keep their previous contents; when
    // that matters for a cell-preserving atlas, refreshCellMipmaps() rebuilds
    // the touched cell's levels from the updated full image.
    void updateRegion(
        const std::vector<unsigned char>& rgbaPixels,
        int imageWidth,
        int x,
        int y,
        int width,
        int height) const;

    // Refreshes the per-cell mip levels of one grid cell from the current
    // level-0 contents of a cell-preserving texture (see
    // loadCellPreservingFromPixels). Call after updateRegion() so a small edit
    // does not leave minified samples showing the previous cell contents.
    // `cellIndex` is row-major over the cellsPerRow x cellsPerRow grid.
    void refreshCellMipmaps(
        const std::vector<unsigned char>& rgbaPixels,
        int imageWidth,
        int cellsPerRow,
        int cellIndex) const;

    template<typename QualityEnum,
             typename = std::enable_if_t<std::is_enum<QualityEnum>::value>>
    static Texture2D loadFromFile(
        const std::string& path,
        QualityEnum quality,
        bool flipVertical = false) {
        return loadFromFile(
            path,
            static_cast<uniscope::d2::TextureQuality>(static_cast<int>(quality)),
            flipVertical);
    }

    template<typename QualityEnum,
             typename = std::enable_if_t<std::is_enum<QualityEnum>::value>>
    static Texture2D loadFromPixels(
        const std::vector<unsigned char>& pixels,
        int width,
        int height,
        QualityEnum quality,
        int channels = 4) {
        return loadFromPixels(
            pixels,
            width,
            height,
            static_cast<uniscope::d2::TextureQuality>(static_cast<int>(quality)),
            channels);
    }

    template<typename QualityEnum,
             typename = std::enable_if_t<std::is_enum<QualityEnum>::value>>
    static Texture2D loadCellPreservingFromPixels(
        const std::vector<unsigned char>& pixels,
        int width,
        int height,
        int cellsPerRow,
        QualityEnum quality,
        int cellsPerColumn = -1,
        int channels = 4) {
        return loadCellPreservingFromPixels(
            pixels,
            width,
            height,
            cellsPerRow,
            static_cast<uniscope::d2::TextureQuality>(static_cast<int>(quality)),
            cellsPerColumn,
            channels);
    }

    template<typename QualityEnum,
             typename = std::enable_if_t<std::is_enum<QualityEnum>::value>>
    void applyFilter(QualityEnum quality) const {
        applyFilter(static_cast<uniscope::d2::TextureQuality>(static_cast<int>(quality)));
    }

    bool valid() const { return textureId_ != 0; }
    GLuint id() const { return textureId_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float aspect() const;

private:
    static Texture2D uploadPixels(
        const unsigned char* pixels,
        int width,
        int height,
        int channels,
        uniscope::d2::TextureQuality quality,
        bool flipVertical = false,
        TextureWrap2D wrap = TextureWrap2D::ClampToEdge);

    GLuint textureId_ = 0;
    int width_ = 0;
    int height_ = 0;
    bool hasMipmaps_ = false;
};

} // namespace uniscope::d2

#endif
