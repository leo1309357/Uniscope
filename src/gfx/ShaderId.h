/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_SHADER_ID_H
#define UNISCOPE_D2_GFX_SHADER_ID_H

namespace uniscope::d2 {

enum class ShaderId {
    Rect2D,
    Rect2DBatch,
    Textured2D,
    Textured2DBatch,
    SdfTextBatch,
    BackdropBlur,
    MenuBackground,
    CinematicBackdrop,
    WhiteOut,
    Upscale,
    ShadowMap,
    ShadowBlur,
    ShadowPolygon
};

} // namespace uniscope::d2

#endif
