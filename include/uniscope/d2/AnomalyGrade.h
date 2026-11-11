/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ANOMALY_GRADE_H
#define UNISCOPE_D2_ANOMALY_GRADE_H

namespace uniscope::d2 {

class Renderer2D;

// One named look for a supernatural phenomenon. Fog drives the world-sprite
// shader directly (see Renderer2D::setFog); the vignette tint is drawn by the
// game layer as a fullscreen pulse/frame, because only the game knows the
// player's exposure (entering, dwelling, watch-twist flash).
//
// Convention, and deliberately fixed: warmth reads as lure (the lakeside
// mirage), amber as decay/heat, cyan-violet as stasis. Do not swap them per
// scene - the whole horror grammar depends on warmth meaning "beautiful and
// lying".
struct AnomalyGrade {
    float fogRed = 0.62f;
    float fogGreen = 0.68f;
    float fogBlue = 0.75f;
    float fogDensity = 0.0f;
    float vignetteRed = 0.0f;
    float vignetteGreen = 0.0f;
    float vignetteBlue = 0.0f;
    float vignetteStrength = 0.0f;
};

namespace anomalyGrades {

// No phenomenon: clear fog, no vignette.
AnomalyGrade neutral();
// Entropy Gain: hot amber-grey air, ember edge pulse.
AnomalyGrade gain();
// Entropy Decay: cold cyan-violet air, frost edge pulse.
AnomalyGrade decay();
// Lakeside mirage: warm sepia air, honeyed edge pulse. Beautiful on purpose.
AnomalyGrade mirage();
// Pocket-watch twist: pale gold flash grade, strong edge pulse.
AnomalyGrade watchFlash();

} // namespace anomalyGrades

AnomalyGrade mixAnomalyGrades(const AnomalyGrade& from, const AnomalyGrade& to, float t);

// Pushes the fog half of a grade into the sprite batch. Vignette stays with
// the game layer (see above).
void applyAnomalyGrade(Renderer2D& renderer, const AnomalyGrade& grade, float fogCenterX, float fogCenterY);

} // namespace uniscope::d2

#endif
