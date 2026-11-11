/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_SHADOW_CASTER_H
#define UNISCOPE_D2_SHADOW_CASTER_H

#include <algorithm>
#include <cmath>
#include <vector>

namespace uniscope::d2 {

// Floor for the along-fall fade: never let a shadow's far end go below this
// fraction of its own strength. A single-segment caster (a great tree, a relief
// cell) has no contact line of its own, so all of its crown vertices sit one
// full extrusion out and every one of them lands on the fade's far end: with a
// zero floor the tree shadow vanished entirely and the morning probe measured
// west/east at 0.978, i.e. no shadow at all. Buildings are unaffected - their
// fade starts from their real footprint line.
inline constexpr float kShadowFadeStart = 0.40f;
inline constexpr float kShadowFadeEnd = 1.00f;
inline constexpr float kShadowFadeFloor = 0.60f;

// Along-fall alpha for an extruded vertex. `along` is the vertex's distance
// from the caster's contact line along the fall direction and `reach` is the
// polygon's own furthest extent, both in the same units, so the fade is
// resolution- and zoom-independent. Returns kShadowFadeFloor..1.
//
// Shared by the building/tree hulls and the character silhouette: both feed a
// polygon into the same max-blended shadow map, so a second copy of these
// constants is how the two halves of one frame's shade drift apart.
inline float shadowAlphaAlongFall(float along, float reach) {
    if (reach <= 0.0001f || along <= 0.0f) {
        return 1.0f;
    }
    const float t =
        (along / reach - kShadowFadeStart) / (kShadowFadeEnd - kShadowFadeStart);
    const float ramp = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return kShadowFadeFloor + (1.0f - kShadowFadeFloor) * ramp;
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// One extruded shadow vertex. `alpha` is the per-vertex diagonal weight: 1 at
// the caster's contact line, fading to kShadowFadeFloor at the throw's far tip.
// The polygon as a whole still carries its own alpha, so the two multiply - a
// caster that is intrinsically faint stays faint, and only the falloff along
// the shadow is per-vertex.
struct ShadowVertex2D {
    float x = 0.0f;
    float y = 0.0f;
    float alpha = 1.0f;
};

struct ShadowPolygon2D {
    std::vector<ShadowVertex2D> vertices;
    float alpha = 1.0f;
};

class ShadowCaster2D {
public:
    void clear();
    void addConvexPolygon(std::vector<Vec2> vertices, float alpha = 1.0f);
    // Same, with a per-vertex diagonal weight (1 at the contact line, fading
    // to kShadowFadeFloor at the far tip). Used for the falloff along a long
    // throw.
    void addConvexPolygonWeighted(
        std::vector<ShadowVertex2D> vertices, float alpha = 1.0f);

    const std::vector<ShadowPolygon2D>& polygons() const { return polygons_; }

private:
    std::vector<ShadowPolygon2D> polygons_;
};

} // namespace uniscope::d2

#endif
