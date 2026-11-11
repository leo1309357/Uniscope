/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_SHADER_ASSETS_H
#define UNISCOPE_D2_GFX_SHADER_ASSETS_H

#include "ShaderId.h"

#include <stdexcept>
#include <string>

namespace uniscope::d2 {

struct ShaderAssetPaths {
    std::string vertex;
    std::string fragment;
};

inline ShaderAssetPaths shaderAssetPaths(ShaderId id) {
    switch (id) {
    case ShaderId::Rect2D:
        return {"shaders/rect2d.vert", "shaders/rect2d.frag"};
    case ShaderId::Rect2DBatch:
        return {"shaders/rect2d_batch.vert", "shaders/rect2d_batch.frag"};
    case ShaderId::Textured2D:
        return {"shaders/textured2d.vert", "shaders/textured2d.frag"};
    case ShaderId::Textured2DBatch:
        return {"shaders/textured2d_batch.vert", "shaders/textured2d_batch.frag"};
    case ShaderId::SdfTextBatch:
        return {"shaders/sdf_text_batch.vert", "shaders/sdf_text_batch.frag"};
    case ShaderId::BackdropBlur:
        return {"shaders/backdrop_blur.vert", "shaders/backdrop_blur.frag"};
    case ShaderId::MenuBackground:
        return {"shaders/menu_background.vert", "shaders/menu_background.frag"};
    case ShaderId::CinematicBackdrop:
        return {"shaders/cinematic_backdrop.vert", "shaders/cinematic_backdrop.frag"};
    case ShaderId::WhiteOut:
        return {"shaders/whiteout.vert", "shaders/whiteout.frag"};
    case ShaderId::Upscale:
        return {"shaders/upscale.vert", "shaders/upscale.frag"};
    case ShaderId::ShadowMap:
        // Reuses the same batched vertex layout; only the fragment shader
        // differs so it can write shadow coverage instead of lit colour.
        return {"shaders/textured2d_batch.vert", "shaders/shadowmap.frag"};
    case ShaderId::ShadowBlur:
        return {"shaders/upscale.vert", "shaders/shadow_blur.frag"};
    case ShaderId::ShadowPolygon:
        return {"shaders/shadow_polygon.vert", "shaders/shadow_polygon.frag"};
    }

    throw std::invalid_argument("Unknown shader id");
}

} // namespace uniscope::d2

#endif
