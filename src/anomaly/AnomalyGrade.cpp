/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/AnomalyGrade.h"

#include "gfx/Renderer2D.h"

namespace uniscope::d2 {

namespace anomalyGrades {

AnomalyGrade neutral() {
    return AnomalyGrade{};
}

AnomalyGrade gain() {
    AnomalyGrade grade;
    grade.fogRed = 0.55f;
    grade.fogGreen = 0.42f;
    grade.fogBlue = 0.30f;
    grade.fogDensity = 0.35f;
    grade.vignetteRed = 0.75f;
    grade.vignetteGreen = 0.30f;
    grade.vignetteBlue = 0.10f;
    grade.vignetteStrength = 0.50f;
    return grade;
}

AnomalyGrade decay() {
    AnomalyGrade grade;
    grade.fogRed = 0.35f;
    grade.fogGreen = 0.50f;
    grade.fogBlue = 0.62f;
    grade.fogDensity = 0.35f;
    grade.vignetteRed = 0.55f;
    grade.vignetteGreen = 0.75f;
    grade.vignetteBlue = 0.90f;
    grade.vignetteStrength = 0.50f;
    return grade;
}

AnomalyGrade mirage() {
    AnomalyGrade grade;
    grade.fogRed = 0.72f;
    grade.fogGreen = 0.60f;
    grade.fogBlue = 0.44f;
    grade.fogDensity = 0.28f;
    grade.vignetteRed = 0.85f;
    grade.vignetteGreen = 0.62f;
    grade.vignetteBlue = 0.35f;
    grade.vignetteStrength = 0.35f;
    return grade;
}

AnomalyGrade watchFlash() {
    AnomalyGrade grade;
    grade.fogRed = 0.80f;
    grade.fogGreen = 0.72f;
    grade.fogBlue = 0.55f;
    grade.fogDensity = 0.15f;
    grade.vignetteRed = 1.00f;
    grade.vignetteGreen = 0.85f;
    grade.vignetteBlue = 0.55f;
    grade.vignetteStrength = 0.80f;
    return grade;
}

} // namespace anomalyGrades

AnomalyGrade mixAnomalyGrades(const AnomalyGrade& from, const AnomalyGrade& to, float t) {
    const float clamped = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    AnomalyGrade mixed;
    mixed.fogRed = from.fogRed + (to.fogRed - from.fogRed) * clamped;
    mixed.fogGreen = from.fogGreen + (to.fogGreen - from.fogGreen) * clamped;
    mixed.fogBlue = from.fogBlue + (to.fogBlue - from.fogBlue) * clamped;
    mixed.fogDensity = from.fogDensity + (to.fogDensity - from.fogDensity) * clamped;
    mixed.vignetteRed = from.vignetteRed + (to.vignetteRed - from.vignetteRed) * clamped;
    mixed.vignetteGreen = from.vignetteGreen + (to.vignetteGreen - from.vignetteGreen) * clamped;
    mixed.vignetteBlue = from.vignetteBlue + (to.vignetteBlue - from.vignetteBlue) * clamped;
    mixed.vignetteStrength =
        from.vignetteStrength + (to.vignetteStrength - from.vignetteStrength) * clamped;
    return mixed;
}

void applyAnomalyGrade(Renderer2D& renderer, const AnomalyGrade& grade, float fogCenterX, float fogCenterY) {
    renderer.setFog(grade.fogRed, grade.fogGreen, grade.fogBlue, grade.fogDensity);
    renderer.setFogCenter(fogCenterX, fogCenterY);
}

} // namespace uniscope::d2
