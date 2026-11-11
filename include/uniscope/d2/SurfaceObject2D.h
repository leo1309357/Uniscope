/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_SURFACE_OBJECT2D_H
#define UNISCOPE_D2_SURFACE_OBJECT2D_H

#include "uniscope/d2/SceneObject2D.h"

#include <algorithm>

namespace uniscope::d2 {

// Surface roles for renderable scene objects. Each is a pure interface (no
// data) so a game entity mixes in exactly what it needs via multiple
// inheritance alongside SceneObject2D.
class IReflectiveObject2D {
public:
    virtual ~IReflectiveObject2D() = default;
    // 0 = matte, 1 = mirror-wet. Drives the shader specular term.
    virtual float reflectivity() const = 0;
    // Standing water film 0..1; damps micro-relief, sharpens highlights.
    virtual float wetness() const = 0;
    // Specular lobe power; higher = tighter highlight.
    virtual float specularPower() const { return 18.0f; }

    // Specular boost of a wet surface over its dry response.
    float wetSpecularBoost() const {
        return std::clamp(wetness(), 0.0f, 1.0f) * std::clamp(reflectivity(), 0.0f, 1.0f);
    }
};

// Backwards-friendly alias requested by game code.
using ReflectiveObject2D = IReflectiveObject2D;

// Heightfield occluder: contributes mass to the ambient-occlusion bake.
// Height in tiles; below the caster threshold it only shades itself.
class IOccluderObject2D {
public:
    virtual ~IOccluderObject2D() = default;
    virtual float occluderHeight() const = 0;
    virtual bool occludesAmbient() const { return occluderHeight() >= 1.0f; }
};

// Shadow caster: ground-projected polygon + opacity. Length scale tunes per
// species (church spires throw farther than sheds); opacity tunes darkness.
class IShadowCasterObject2D {
public:
    virtual ~IShadowCasterObject2D() = default;
    virtual bool castsShadow() const = 0;
    virtual float shadowOpacity() const { return 1.0f; }
    virtual float shadowLengthScale() const { return 1.0f; }
    // Contact blob alpha for the soft ellipse at the base.
    virtual float contactShadowAlpha() const { return 0.22f; }
};

// Shadow receiver: anything the sun-shadow system may darken.
class IShadowReceiverObject2D {
public:
    virtual ~IShadowReceiverObject2D() = default;
    virtual bool receivesShadow() const { return true; }
    virtual void applyShadowFactor(float /*darken*/) {}
};

} // namespace uniscope::d2

#endif
