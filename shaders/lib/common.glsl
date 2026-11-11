// Shared Uniscope-2D shader helpers.
// Pulled in by the ShaderProgram include resolver (see lib/common.glsl usage in
// sibling shaders). Kept dependency-free: only built-in GLSL 330 functionality.

#ifndef UNISCOPE_COMMON_GLSL
#define UNISCOPE_COMMON_GLSL

// ---- Effect quality tiers -------------------------------------------------
// Kept in one place so tier thresholds do not drift between shaders.
#define UNISCOPE_QUALITY_T1 0.30
#define UNISCOPE_QUALITY_T2 0.65
#define UNISCOPE_QUALITY_T3 0.95

// ---- Hashes ---------------------------------------------------------------
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash11(float p) {
    return hash12(vec2(p, p * 1.7 + 11.3));
}

// ---- Value noise ----------------------------------------------------------
float valueNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 local = fract(p);
    local = local * local * (3.0 - 2.0 * local);
    return mix(
        mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), local.x),
        mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0, 1.0)), local.x),
        local.y);
}

// ---- Tone mapping ---------------------------------------------------------
// Single curve used by every shader. `shoulder` controls highlight roll-off and
// `gain`/`gamma` shape the output. Callers pass their tuned constants so the
// per-material look is preserved while the implementation stays in one place.
vec3 toneMap(vec3 color, float shoulder, float gain, float gamma) {
    color = max(color, vec3(0.0));
    color = color / (color + vec3(shoulder));
    return pow(color * gain, vec3(gamma));
}

// Default curve — used where a shader has no custom tuning.
vec3 toneMap(vec3 color) {
    return toneMap(color, 0.94, 1.42, 0.92);
}

#endif // UNISCOPE_COMMON_GLSL
