/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_TEXTURE2DARRAY_H
#define UNISCOPE_D2_GFX_TEXTURE2DARRAY_H

#include "uniscope/d2/Types.h"

#include "GlCompat.h"

#include <type_traits>
#include <vector>

namespace uniscope::d2 {

// A stack of same-sized RGBA8 layers in one GL_TEXTURE_2D_ARRAY.
//
// This exists for the world detail map. A single 2D texture carrying one detail
// field means every ground material shares it, so grass, rock, sand and snow all
// get the same micro-structure and the materials differ only in their albedo -
// which is exactly the part the light is least able to tell apart. Giving each
// material its own layer fixes that, and it has to be a *layer* rather than a
// cell in a 2D atlas for two reasons:
//
//   - GL_REPEAT is per-layer here, so a layer tiles inside itself with no
//     fract(), no gutter ring and no mip level ever averaging two materials
//     together. A 2D cell atlas has to re-derive the gutter for every cell and
//     still bleeds across the seam at the coarse mip levels.
//   - The world detail map is sampled from the fragment's *clip position*, not
//     from the quad, so a field of one material is a single continuous pattern
//     that happens to be wrapped into 0..1. A cell inset would put a hard
//     discontinuity back into that coordinate.
class Texture2DArray {
public:
    Texture2DArray() = default;
    ~Texture2DArray();

    Texture2DArray(const Texture2DArray&) = delete;
    Texture2DArray& operator=(const Texture2DArray&) = delete;

    Texture2DArray(Texture2DArray&& other) noexcept;
    Texture2DArray& operator=(Texture2DArray&& other) noexcept;

    // Uploads one layer per element of `layers`, in order. `width` x `height`
    // texels each, RGBA8, tightly packed rows, top-left origin as
    // Texture2D::loadTilingFromPixels expects. Uploads with GL_REPEAT wrapping
    // and a full mip chain, because a world-space detail field is minified
    // heavily at gameplay zoom and an unmipped one aliases into stipple.
    //
    // Returns an invalid texture if any layer is the wrong size or the list is
    // empty, so a caller cannot end up sampling past the end of the array.
    // `quality` picks the sampler state (see TextureFilter.h), like every other
    // upload; it used to be pinned at High here regardless of the tier.
    static Texture2DArray loadTilingFromLayers(
        const std::vector<std::vector<unsigned char>>& layers,
        int width,
        int height,
        uniscope::d2::TextureQuality quality = uniscope::d2::TextureQuality::High);

    template<typename QualityEnum,
             typename = std::enable_if_t<std::is_enum<QualityEnum>::value>>
    static Texture2DArray loadTilingFromLayers(
        const std::vector<std::vector<unsigned char>>& layers,
        int width,
        int height,
        QualityEnum quality) {
        return loadTilingFromLayers(
            layers,
            width,
            height,
            static_cast<uniscope::d2::TextureQuality>(static_cast<int>(quality)));
    }

    void applyFilter(uniscope::d2::TextureQuality quality) const;

    // Uploads one layer per element of `layers`, in order. `width` x `height`
    // texels each, RGBA8. Unlike loadTilingFromLayers, this uses CLAMP_TO_EDGE
    // wrapping (no repeat) and generates a full mip chain. Intended for
    // non-tiling textures like building elevations where each layer is a
    // complete sprite. Returns an invalid texture if any layer is the wrong size.
    static Texture2DArray loadFromLayers(
        const std::vector<std::vector<unsigned char>>& layers,
        int width,
        int height,
        uniscope::d2::TextureQuality quality = uniscope::d2::TextureQuality::High);

    template<typename QualityEnum,
             typename = std::enable_if_t<std::is_enum<QualityEnum>::value>>
    static Texture2DArray loadFromLayers(
        const std::vector<std::vector<unsigned char>>& layers,
        int width,
        int height,
        QualityEnum quality) {
        return loadFromLayers(
            layers,
            width,
            height,
            static_cast<uniscope::d2::TextureQuality>(static_cast<int>(quality)));
    }

    bool valid() const { return textureId_ != 0; }
    GLuint id() const { return textureId_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int layers() const { return layers_; }

private:
    GLuint textureId_ = 0;
    int width_ = 0;
    int height_ = 0;
    int layers_ = 0;
};

} // namespace uniscope::d2

#endif