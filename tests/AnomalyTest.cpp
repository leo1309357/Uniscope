/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/AnomalyField.h"
#include "uniscope/d2/AnomalyGrade.h"

#include <cmath>
#include <cstdio>

namespace {

bool check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "anomaly check failed: %s\n", message);
        return false;
    }
    return true;
}

bool inRange(float value) {
    return value >= 0.0f && value <= 1.0f;
}

} // namespace

int main() {
    using namespace uniscope::d2;

    // Determinism: same input, same sample, every time.
    {
        AnomalyField field(1234u);
        const AnomalySample a = field.sample(10.5f, -3.25f, 42.0f);
        const AnomalySample b = field.sample(10.5f, -3.25f, 42.0f);
        if (!check(
                a.gain == b.gain && a.decay == b.decay && a.clash == b.clash && a.edge == b.edge,
                "field sampling is deterministic")) {
            return 1;
        }
    }

    // Range and coverage: every channel stays in [0,1] over a wide sweep that
    // includes negative coordinates and several timestamps, and both states
    // actually occur somewhere (a field with no zones is a dead field).
    {
        AnomalyField field(777u);
        float maxGain = 0.0f;
        float maxDecay = 0.0f;
        float maxEdge = 0.0f;
        for (int t = 0; t < 4; ++t) {
            const float time = static_cast<float>(t) * 37.0f;
            for (int y = -20; y <= 20; ++y) {
                for (int x = -20; x <= 20; ++x) {
                    const AnomalySample s = field.sample(
                        static_cast<float>(x) * 1.7f, static_cast<float>(y) * 1.7f, time);
                    if (!check(
                            inRange(s.gain) && inRange(s.decay) && inRange(s.clash)
                                && inRange(s.edge),
                            "sample channels stay in [0,1]")) {
                        return 1;
                    }
                    // Clash is the geometric mean: only both-strong lights it,
                    // and it always lands between the two parent channels.
                    const float expected = std::sqrt(s.gain * s.decay);
                    const float lo = s.gain < s.decay ? s.gain : s.decay;
                    const float hi = s.gain > s.decay ? s.gain : s.decay;
                    if (!check(
                            std::fabs(s.clash - expected) < 1e-5f && s.clash + 1e-5f >= lo
                                && s.clash <= hi + 1e-5f,
                            "clash is the bounded geometric mean")) {
                        return 1;
                    }
                    if (s.gain > maxGain) {
                        maxGain = s.gain;
                    }
                    if (s.decay > maxDecay) {
                        maxDecay = s.decay;
                    }
                    if (s.edge > maxEdge) {
                        maxEdge = s.edge;
                    }
                }
            }
        }
        std::printf("anomaly field coverage: gain=%.3f decay=%.3f edge=%.3f\n", maxGain, maxDecay, maxEdge);
        if (!check(maxGain > 0.3f && maxDecay > 0.3f, "both entropy states occur")) {
            return 1;
        }
        if (!check(maxEdge > 0.3f, "transition bands occur")) {
            return 1;
        }
    }

    // Continuity: a small step in world space is a small step in the field
    // (no popping when the player walks).
    {
        AnomalyField field(999u);
        for (int i = 0; i < 200; ++i) {
            const float x = static_cast<float>(i) * 0.61f - 50.0f;
            const float y = static_cast<float>(i) * 1.13f - 30.0f;
            const AnomalySample a = field.sample(x, y, 10.0f);
            const AnomalySample b = field.sample(x + 0.05f, y, 10.0f);
            const float jump = std::fabs(a.gain - b.gain) + std::fabs(a.decay - b.decay);
            if (!check(jump < 0.05f, "field is continuous under small steps")) {
                return 1;
            }
        }
    }

    // Seeds matter, and drift moves zones without breaking bounds.
    {
        AnomalyField a(1u);
        AnomalyField b(2u);
        bool differs = false;
        for (int i = 0; i < 100 && !differs; ++i) {
            const float x = static_cast<float>(i) * 0.77f;
            const AnomalySample sa = a.sample(x, 3.0f, 0.0f);
            const AnomalySample sb = b.sample(x, 3.0f, 0.0f);
            differs = std::fabs(sa.gain - sb.gain) > 0.01f || std::fabs(sa.decay - sb.decay) > 0.01f;
        }
        if (!check(differs, "different seeds give different fields")) {
            return 1;
        }

        AnomalyField still(55u, 0.08f, 0.0f);
        const AnomalySample t0 = still.sample(4.0f, 5.0f, 0.0f);
        const AnomalySample t1 = still.sample(4.0f, 5.0f, 1000.0f);
        if (!check(
                t0.gain == t1.gain && t0.decay == t1.decay, "zero drift freezes time")) {
            return 1;
        }

        AnomalyField drifting(55u);
        const AnomalySample d0 = drifting.sample(4.0f, 5.0f, 0.0f);
        const AnomalySample d1 = drifting.sample(4.0f, 5.0f, 500.0f);
        if (!check(
                inRange(d1.gain) && inRange(d1.decay)
                    && (std::fabs(d0.gain - d1.gain) > 0.001f
                        || std::fabs(d0.decay - d1.decay) > 0.001f),
                "drift moves zones over time")) {
            return 1;
        }
    }

    // Grades: neutral is empty, presets are sane, mixing is exact at the ends
    // and clamped outside [0,1].
    {
        using namespace anomalyGrades;
        const AnomalyGrade clear = neutral();
        if (!check(clear.fogDensity == 0.0f && clear.vignetteStrength == 0.0f, "neutral grade is empty")) {
            return 1;
        }
        const AnomalyGrade presets[] = {gain(), decay(), mirage(), watchFlash()};
        for (const AnomalyGrade& preset : presets) {
            if (!check(
                    preset.fogDensity > 0.0f && preset.fogDensity <= 1.0f
                        && preset.vignetteStrength >= 0.0f && preset.vignetteStrength <= 1.0f,
                    "preset grade stays in range")) {
                return 1;
            }
        }
        // Warmth grammar: the mirage is the warmest fog, decay the coldest.
        if (!check(
                mirage().fogRed > decay().fogRed && mirage().fogBlue < decay().fogBlue,
                "mirage reads warmer than decay")) {
            return 1;
        }
        const AnomalyGrade from = gain();
        const AnomalyGrade to = decay();
        const AnomalyGrade atZero = mixAnomalyGrades(from, to, 0.0f);
        const AnomalyGrade atOne = mixAnomalyGrades(from, to, 1.0f);
        const AnomalyGrade half = mixAnomalyGrades(from, to, 0.5f);
        if (!check(
                atZero.fogDensity == from.fogDensity && atOne.fogDensity == to.fogDensity,
                "grade mixing is exact at the ends")) {
            return 1;
        }
        if (!check(
                half.fogDensity == (from.fogDensity + to.fogDensity) * 0.5f,
                "grade mixing lerps at the middle")) {
            return 1;
        }
        const AnomalyGrade clamped = mixAnomalyGrades(from, to, 5.0f);
        if (!check(clamped.fogDensity == to.fogDensity, "grade mixing clamps t")) {
            return 1;
        }
    }

    std::printf("anomaly: all checks passed\n");
    return 0;
}
