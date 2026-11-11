/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_LIGHT_SOURCE2D_H
#define UNISCOPE_D2_LIGHT_SOURCE2D_H

#include "uniscope/d2/SceneObject2D.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace uniscope::d2 {

struct LightColor2D {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

enum class LightSourceKind {
    Ambient,
    Directional,
    Point,
};

// Base class for every light: ambient fill, sun/moon directionals and point
// lights share identity + switch. Concrete lights add their own parameters.
// A light is never drawable itself; the lighting rig samples it per frame.
class LightSource2D : public SceneObject2D {
public:
    explicit LightSource2D(LightSourceKind kind) : kind_(kind) {}
    LightSourceKind kind() const { return kind_; }

    LightColor2D color() const { return color_; }
    void setColor(LightColor2D color) { color_ = color; }

    float intensity() const { return intensity_; }
    void setIntensity(float intensity) { intensity_ = intensity < 0.0f ? 0.0f : intensity; }

    bool enabled() const { return enabled_ && visible(); }
    void setEnabled(bool enabled) { enabled_ = enabled; }

protected:
    LightSourceKind kind_;
    LightColor2D color_{1.0f, 1.0f, 1.0f};
    float intensity_ = 1.0f;
    bool enabled_ = true;
};

// Hemisphere fill: colour multiplied by intensity, added after the shadow
// modulation so shadowed areas still receive it. Intensity may exceed 1.0 to
// lift night or overcast scenes.
class AmbientLight2D : public LightSource2D {
public:
    AmbientLight2D() : LightSource2D(LightSourceKind::Ambient) {}
    AmbientLight2D(LightColor2D color, float intensity) : LightSource2D(LightSourceKind::Ambient) {
        setColor(color);
        setIntensity(intensity);
    }
};

// Sun / moon: unit direction TOWARD the source in world space (+X east,
// +Y north), plus elevation 0 (horizon) .. 1 (zenith) driving shadow length
// and colour warmth. Cardinal arc: east -> north (noon) -> west.
class DirectionalLight2D : public LightSource2D {
public:
    DirectionalLight2D() : LightSource2D(LightSourceKind::Directional) {}

    float dirX() const { return dirX_; }
    float dirY() const { return dirY_; }
    float elevation() const { return elevation_; }

    void setDirection(float x, float y, float elevation01) {
        const float len = std::sqrt(x * x + y * y);
        if (len > 0.0001f) {
            dirX_ = x / len;
            dirY_ = y / len;
        } else {
            dirX_ = 0.0f;
            dirY_ = 1.0f;
        }
        elevation_ = std::clamp(elevation01, 0.0f, 1.0f);
    }

    // Shadow fall direction (opposite the light).
    float fallX() const { return -dirX_; }
    float fallY() const { return -dirY_; }

private:
    float dirX_ = 0.0f;
    float dirY_ = 1.0f;
    float elevation_ = 1.0f;
};

// Point light: quadratic falloff to radius, optional shadow casting.
class PointLightSource2D : public LightSource2D {
public:
    PointLightSource2D() : LightSource2D(LightSourceKind::Point) {}

    float radius() const { return radius_; }
    void setRadius(float radius) { radius_ = radius > 0.0f ? radius : 0.0f; }
    bool castShadows() const { return castShadows_; }
    void setCastShadows(bool cast) { castShadows_ = cast; }

    // 0 outside the radius, 1 at the centre (squared falloff * intensity).
    float falloff(float distance) const {
        if (radius_ <= 0.0f || distance >= radius_ || !enabled()) {
            return 0.0f;
        }
        const float t = 1.0f - distance / radius_;
        return t * t * intensity();
    }

private:
    float radius_ = 6.0f;
    bool castShadows_ = false;
};

// Per-frame lighting rig: one ambient + one directional + N points. Game code
// fills it once per frame; WorldRenderer forwards the solved values to the
// Renderer2D setters. Keeps raw-float plumbing in one place.
class LightingRig2D {
public:
    AmbientLight2D& ambient() { return ambient_; }
    const AmbientLight2D& ambient() const { return ambient_; }
    DirectionalLight2D& sun() { return sun_; }
    const DirectionalLight2D& sun() const { return sun_; }

    void clearPoints() { points_.clear(); }
    void addPoint(const PointLightSource2D& light) { points_.push_back(light); }
    const std::vector<PointLightSource2D>& points() const { return points_; }

private:
    AmbientLight2D ambient_;
    DirectionalLight2D sun_;
    std::vector<PointLightSource2D> points_;
};

} // namespace uniscope::d2

#endif
