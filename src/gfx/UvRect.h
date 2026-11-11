/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_UV_RECT_H
#define UNISCOPE_D2_GFX_UV_RECT_H

#include <algorithm>

namespace uniscope::d2 {

struct UvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;

    // Which v a sprite quad corner samples, given its local Y (-1 = the
    // quad's bottom edge, +1 = its top edge).
    //
    // The quad's TOP samples v0 and its BOTTOM samples v1. That is the whole
    // orientation of every texture in the game, so it is named rather than
    // inlined: an atlas built top-down (painter row 0 = the frame's north
    // edge) draws upright under this rule, and an atlas built the other way
    // round draws upside down.
    //
    // It is deliberately the opposite of the textbook GL reading, where v0 is
    // the bottom of the image. The atlas is stored bottom-up - frame row 0
    // occupies the last rows of the buffer - so the two cancel out and the
    // art comes out the right way up. Reversing this one line is enough to
    // turn every figure in the game upside down, and nothing else in the
    // pipeline would notice: the ground-line measurement would keep
    // reporting the same number and only the picture would change.
    float vForLocalY(float localY) const {
        return localY < 0.0f ? v1 : v0;
    }

    // The same rule for u: the quad's left edge samples u0.
    float uForLocalX(float localX) const {
        return localX < 0.0f ? u0 : u1;
    }
};

inline UvRect cellUv(
    int column,
    int row,
    int columns,
    int rows,
    int atlasPixelSize = 2048,
    int atlasPixelHeight = 0) {
    const float cellU = 1.0f / static_cast<float>(columns);
    const float cellV = 1.0f / static_cast<float>(rows);
    const float u0 = static_cast<float>(column) * cellU;
    const float u1 = u0 + cellU;
    const float vTop = 1.0f - static_cast<float>(row) * cellV;
    const float vBottom = vTop - cellV;
    const float texelU = static_cast<float>(atlasPixelSize);
    // A non-square atlas has different texel pitches on the two axes; the
    // height argument defaults to 0 (square, same pitch as width) so every
    // existing square-atlas caller keeps its exact semantics unchanged.
    const float texelV = atlasPixelHeight > 0 ? static_cast<float>(atlasPixelHeight) : texelU;
    return {u0 + 0.5f / texelU, vBottom + 0.5f / texelV, u1 - 0.5f / texelU, vTop - 0.5f / texelV};
}

// Same as cellUv but with a symmetric texel inset on every side. Ground cells
// bake a wrap-around gutter around their content and address it with this, so a
// tile quad can sit exactly flush with its neighbours: nothing to bleed, so no
// overlap, and therefore no seam.
// `atlasPixelHeight` behaves exactly as in cellUv: 0 means a square atlas.
inline UvRect cellUvInset(
    int column,
    int row,
    int columns,
    int rows,
    int atlasPixelSize,
    float insetTexels,
    int atlasPixelHeight = 0) {
    const float cellU = 1.0f / static_cast<float>(columns);
    const float cellV = 1.0f / static_cast<float>(rows);
    const float u0 = static_cast<float>(column) * cellU;
    const float u1 = u0 + cellU;
    const float vTop = 1.0f - static_cast<float>(row) * cellV;
    const float vBottom = vTop - cellV;
    const float texelU = static_cast<float>(atlasPixelSize);
    const float texelV = atlasPixelHeight > 0 ? static_cast<float>(atlasPixelHeight) : texelU;
    const float insetU = insetTexels / texelU;
    const float insetV = insetTexels / texelV;
    return {
        std::min(u0 + insetU, u1 - insetU),
        std::min(vBottom + insetV, vTop - insetV),
        std::max(u1 - insetU, u0 + insetU),
        std::max(vTop - insetV, vBottom + insetV)};
}

} // namespace uniscope::d2

#endif
