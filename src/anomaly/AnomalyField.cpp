/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/AnomalyField.h"

#include <cmath>

namespace uniscope::d2 {

AnomalyField::AnomalyField(std::uint64_t seed, float scale, float driftRate)
    : seed_(seed)
    , scale_(scale > 0.0f ? scale : 0.08f)
    , driftRate_(driftRate >= 0.0f ? driftRate : 0.0f) {}

float AnomalyField::hash01(int cellX, int cellY, std::uint64_t seed) {
    // Integer lattice hash; all arithmetic is unsigned so large coordinates
    // cannot invoke signed overflow (which the project forbids depending on).
    std::uint64_t value = static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellX)) * 1619u
        + static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellY)) * 31337u
        + seed * 1013u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return static_cast<float>(value & 0xFFFFu) / 65535.0f;
}

float AnomalyField::valueNoise(float x, float y, std::uint64_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);

    const float a = hash01(ix, iy, seed);
    const float b = hash01(ix + 1, iy, seed);
    const float c = hash01(ix, iy + 1, seed);
    const float d = hash01(ix + 1, iy + 1, seed);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

float AnomalyField::fbm(float x, float y, std::uint64_t seed) {
    // Three octaves, energy-preserving halving. Kept shallow on purpose: zone
    // shapes should read as regions, not as static.
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float normalizer = 0.0f;
    for (int octave = 0; octave < 3; ++octave) {
        sum += valueNoise(x * frequency, y * frequency, seed + static_cast<std::uint64_t>(octave) * 19u)
            * amplitude;
        normalizer += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return normalizer > 0.0f ? sum / normalizer : 0.0f;
}

float AnomalyField::smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = (value - edge0) / (edge1 - edge0);
    const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

AnomalySample AnomalyField::sample(float x, float y, float timeSeconds) const {
    // The drift offset walks the sample point slowly through noise space, so a
    // fixed world position sees its zone breathe and creep. The y drift uses a
    // different rate so the motion is a slow shear rather than a diagonal slide
    // (which would read as wind in a fixed direction).
    const float drift = timeSeconds * driftRate_;
    const float sx = x * scale_ + drift;
    const float sy = y * scale_ + drift * 0.37f;

    const float gainNoise = fbm(sx, sy, seed_ ^ 0x9E3779B97F4A7C15ull);
    const float decayNoise = fbm(sx + 13.7f, sy - 7.3f, seed_ ^ 0xC2B2AE3D27D4EB4Full);

    AnomalySample result;
    result.gain = smoothstep(0.52f, 0.72f, gainNoise);
    result.decay = smoothstep(0.52f, 0.72f, decayNoise);
    // Collision core: geometric mean so it only lights up where BOTH states
    // are strong, never where one merely grazes the threshold.
    result.clash = std::sqrt(result.gain * result.decay);
    // Entry/exit band around the zone boundary for transition feedback.
    const float strongest = result.gain > result.decay ? result.gain : result.decay;
    result.edge = smoothstep(0.30f, 0.48f, strongest) * (1.0f - smoothstep(0.52f, 0.70f, strongest));
    return result;
}

} // namespace uniscope::d2
