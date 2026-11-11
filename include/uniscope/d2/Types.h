/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_TYPES_H
#define UNISCOPE_D2_TYPES_H

namespace uniscope::d2 {

enum class TextureQuality {
    Low,
    Medium,
    High,
    Ultra,
    Count
};

// What one quality tier means to the renderer and to texture sampling.
//
// The tier is the single knob the options menu exposes. Everything the engine
// derives from it is spelled out here, in one table, so a tier cannot mean one
// thing to the shadow pass and another to the texture filter: the two used to be
// decided in different files (the filter in TextureFilter.h, the shader scalar
// in the game's Application.cpp) and nothing tied them together.
struct QualityPreset2D {
    // Shader effect tier scalar, 0..1. The shaders gate their optional terms on
    // UNISCOPE_QUALITY_T1/T2/T3 (lib/common.glsl): grain and subsurface from
    // 0.30, detail-normal lighting and wet specular from 0.65, animated sheen
    // from 0.95.
    float effectQuality = 1.0f;
    // Minification between mip levels: trilinear blends two levels, nearest
    // snaps to one (a visible step where the level changes, but one fetch).
    // Every tier builds mipmaps now; a tier without them shimmered on any
    // minified ground, which is the cheapest aliasing there is to remove.
    bool trilinear = true;
    // Anisotropic filtering level; 1 disables it. Clamped to the driver's
    // maximum when applied.
    float anisotropy = 1.0f;
    // Screen-space shadow map size relative to the framebuffer, 0.25..1. Half
    // resolution is hidden by the sun blur but shows on the hard contact
    // patches; full resolution keeps those crisp.
    float shadowResolutionScale = 0.5f;
    // Dynamic-resolution present: bicubic Catmull-Rom reconstruction instead
    // of bilinear, which keeps edges from going soft when the world pass
    // renders below native resolution.
    bool bicubicUpscale = true;
};

inline const QualityPreset2D& qualityPreset(TextureQuality tier) {
    static const QualityPreset2D kPresets[static_cast<int>(TextureQuality::Count)] = {
        // Low: every optional shader term off, nearest-mip, half-res shadows,
        // bilinear upscale. The floor for old integrated GPUs.
        {0.0f, false, 1.0f, 0.5f, false},
        // Medium: the T1 terms (grain, subsurface, fabric sheen), trilinear
        // with light anisotropy, half-res shadows.
        {0.35f, true, 2.0f, 0.5f, true},
        // High: the T2 terms (detail-normal lighting, wet specular), 8x
        // anisotropy, full-res shadows.
        {0.70f, true, 8.0f, 1.0f, true},
        // Ultra: everything, 16x anisotropy.
        {1.0f, true, 16.0f, 1.0f, true},
    };
    const int index = static_cast<int>(tier);
    if (index < 0 || index >= static_cast<int>(TextureQuality::Count)) {
        return kPresets[static_cast<int>(TextureQuality::High)];
    }
    return kPresets[index];
}

} // namespace uniscope::d2

#endif
