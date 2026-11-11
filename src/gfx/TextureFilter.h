/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_TEXTURE_FILTER_H
#define UNISCOPE_D2_GFX_TEXTURE_FILTER_H

#include "uniscope/d2/Types.h"

#include "GlCompat.h"

namespace uniscope::d2 {

namespace {

float maxSupportedAnisotropy() {
    if (!GLAD_GL_EXT_texture_filter_anisotropic) {
        return 1.0f;
    }

    static bool queried = false;
    static float maxAniso = 1.0f;
    if (!queried) {
        GLfloat driverMax = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &driverMax);
        maxAniso = driverMax > 1.0f ? driverMax : 1.0f;
        queried = true;
    }
    return maxAniso;
}

void applyAnisotropy(uniscope::d2::TextureQuality quality, bool hasMipmaps, GLenum target) {
    if (!GLAD_GL_EXT_texture_filter_anisotropic) {
        return;
    }

    // Anisotropy is a minification filter and needs a mip chain to pick from;
    // on a texture without one it is a no-op the driver still pays for.
    const float desired = hasMipmaps ? qualityPreset(quality).anisotropy : 1.0f;
    const float maxAniso = maxSupportedAnisotropy();
    const float level = desired > maxAniso ? maxAniso : (desired < 1.0f ? 1.0f : desired);
    glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, level);
}

} // namespace

// `target` is GL_TEXTURE_2D everywhere except the world detail map, which is a
// GL_TEXTURE_2D_ARRAY: the sampler state is identical, only the target the
// parameters are set on differs, and keeping one implementation means a filter
// change cannot land on one of them and miss the other.
//
// The filter follows the tier's QualityPreset2D: every tier samples bilinearly
// at magnification, and with a mip chain present the tier only decides whether
// minification blends two levels (trilinear) or snaps to one. The old Low tier
// used GL_NEAREST for both, which pixelated magnified art for no saving a GPU
// made this century can measure.
inline void applyTextureFilter(
    GLuint textureId,
    uniscope::d2::TextureQuality quality,
    bool hasMipmaps = true,
    GLenum target = GL_TEXTURE_2D) {
    if (textureId == 0) {
        return;
    }

    const QualityPreset2D& preset = qualityPreset(quality);
    glBindTexture(target, textureId);
    if (hasMipmaps) {
        glTexParameteri(
            target,
            GL_TEXTURE_MIN_FILTER,
            preset.trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST);
    } else {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    applyAnisotropy(quality, hasMipmaps, target);
    glBindTexture(target, 0);
}

} // namespace uniscope::d2

#endif
