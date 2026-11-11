/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ANOMALY_FIELD_H
#define UNISCOPE_D2_ANOMALY_FIELD_H

#include <cstdint>

namespace uniscope::d2 {

// One sample of the supernatural field ("Causality Corrosion") at a world
// position and time. All channels are 0..1:
//
//   gain  - Entropy Gain: rapid decay, rust, heat, ash.
//   decay - Entropy Decay: stasis, crystallization, reversed growth.
//   clash - where both collide: the fracture-zone core that breeds aberrants.
//   edge  - transition band around the zone boundary, for entry/exit feedback.
//
// Pure data; the game layer decides what each channel drives (fog tint,
// particle rates, damage, watch interactions). Nothing here touches GL, saves,
// or the clock - sampling is a pure function of (x, y, timeSeconds), so it is
// deterministic, headless-testable, and safe to call from any thread.
struct AnomalySample {
    float gain = 0.0f;
    float decay = 0.0f;
    float clash = 0.0f;
    float edge = 0.0f;
};

// Deterministic spatial field for supernatural zones. Two independent fbm
// noise fields (different seeds) are shaped into gain/decay intensities; the
// sample coordinates drift slowly with time so zones "breathe" instead of
// sitting still. Same (seed, x, y, t) always yields the same sample.
class AnomalyField {
public:
    explicit AnomalyField(
        std::uint64_t seed = 0,
        float scale = 0.08f,
        float driftRate = 0.01f);

    AnomalySample sample(float x, float y, float timeSeconds) const;

    void setSeed(std::uint64_t seed) { seed_ = seed; }
    void setScale(float scale) { scale_ = scale; }
    void setDriftRate(float driftRate) { driftRate_ = driftRate; }

    std::uint64_t seed() const { return seed_; }
    float scale() const { return scale_; }
    float driftRate() const { return driftRate_; }

private:
    static float hash01(int cellX, int cellY, std::uint64_t seed);
    static float valueNoise(float x, float y, std::uint64_t seed);
    static float fbm(float x, float y, std::uint64_t seed);
    static float smoothstep(float edge0, float edge1, float value);

    std::uint64_t seed_ = 0;
    float scale_ = 0.08f;
    float driftRate_ = 0.01f;
};

} // namespace uniscope::d2

#endif
