/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RigProportions2D.h"

#include <algorithm>
#include <cmath>

namespace uniscope::d2 {

bool validateRigProportions2D(RigProportions2D& params) {
    const RigProportions2D def;
    bool ok = true;
    const auto fix = [&](float& field, float lo, float hi, float fallback) {
        if (!std::isfinite(field)) {
            field = fallback;
            ok = false;
            return;
        }
        const float clamped = std::clamp(field, lo, hi);
        if (clamped != field) {
            field = clamped;
            ok = false;
        }
    };
    fix(params.pelvis, 0.03f, 0.15f, def.pelvis);
    fix(params.spineLow, 0.03f, 0.20f, def.spineLow);
    fix(params.spineMid, 0.03f, 0.20f, def.spineMid);
    fix(params.chest, 0.03f, 0.20f, def.chest);
    fix(params.neck, 0.005f, 0.10f, def.neck);
    fix(params.head, 0.08f, 0.25f, def.head);
    fix(params.clavicle, 0.02f, 0.10f, def.clavicle);
    fix(params.upperArm, 0.05f, 0.30f, def.upperArm);
    fix(params.forearm, 0.05f, 0.30f, def.forearm);
    fix(params.hand, 0.02f, 0.10f, def.hand);
    fix(params.thigh, 0.05f, 0.30f, def.thigh);
    fix(params.shin, 0.05f, 0.30f, def.shin);
    fix(params.foot, 0.03f, 0.15f, def.foot);
    // Width bands must contain bulk 1.3x of the shipped defaults (widest is the
    // boot at 1.10, so 1.45): otherwise applyRigBulk2D's own documented range
    // reports failure and a legal bulk edit silently gets clamped.
    fix(params.thighWidth, 0.30f, 1.45f, def.thighWidth);
    fix(params.shinWidth, 0.30f, 1.45f, def.shinWidth);
    fix(params.footWidth, 0.30f, 1.45f, def.footWidth);
    fix(params.upperArmWidth, 0.30f, 1.45f, def.upperArmWidth);
    fix(params.forearmWidth, 0.30f, 1.45f, def.forearmWidth);
    fix(params.handScale, 0.40f, 1.60f, def.handScale);
    fix(params.shoulderScale, 0.10f, 1.60f, def.shoulderScale);
    fix(params.headScale, 0.40f, 1.60f, def.headScale);
    fix(params.hairScale, 0.40f, 1.60f, def.hairScale);
    fix(params.eyeScale, 0.40f, 1.60f, def.eyeScale);
    fix(params.pelvisThickness, 0.02f, 0.15f, def.pelvisThickness);
    fix(params.torsoLowerThickness, 0.02f, 0.15f, def.torsoLowerThickness);
    fix(params.torsoUpperThickness, 0.02f, 0.15f, def.torsoUpperThickness);
    fix(params.coatThickness, 0.02f, 0.15f, def.coatThickness);
    fix(params.beltThickness, 0.02f, 0.15f, def.beltThickness);
    fix(params.limbOverlap, 1.00f, 1.30f, def.limbOverlap);
    // Taper bands mirror testLimbsTaperTowardTheTip: a far end below 0.55 stops
    // reading as a limb, and a near end above 1.001 stops reading as a taper.
    const auto fixTaper = [&](float& field, float fallback) {
        fix(field, 0.55f, 1.001f, fallback);
    };
    fixTaper(params.thighTaperFar, def.thighTaperFar);
    fixTaper(params.shinTaperNear, def.shinTaperNear);
    fixTaper(params.shinTaperFar, def.shinTaperFar);
    fixTaper(params.upperArmTaperFar, def.upperArmTaperFar);
    fixTaper(params.forearmTaperNear, def.forearmTaperNear);
    fixTaper(params.forearmTaperFar, def.forearmTaperFar);
    fixTaper(params.pelvisTaperNear, def.pelvisTaperNear);
    fixTaper(params.torsoLowerTaperNear, def.torsoLowerTaperNear);
    return ok;
}

void scaleRigProportions2D(RigProportions2D& p, float scale) {
    // Bone lengths carry every derived girth with them (limb widths, blob
    // radii and braid lengths are ratios against a bone length), so scaling
    // them applies the factor exactly once. The authored torso half-thicknesses
    // are the only absolute girth values and are scaled explicitly.
    p.pelvis *= scale;
    p.spineLow *= scale;
    p.spineMid *= scale;
    p.chest *= scale;
    p.neck *= scale;
    p.head *= scale;
    p.clavicle *= scale;
    p.upperArm *= scale;
    p.forearm *= scale;
    p.hand *= scale;
    p.thigh *= scale;
    p.shin *= scale;
    p.foot *= scale;
    p.pelvisThickness *= scale;
    p.torsoLowerThickness *= scale;
    p.torsoUpperThickness *= scale;
    p.coatThickness *= scale;
    p.beltThickness *= scale;
}

void applyRigBulk2D(RigProportions2D& p, float scale) {
    p.thighWidth *= scale;
    p.shinWidth *= scale;
    p.footWidth *= scale;
    p.upperArmWidth *= scale;
    p.forearmWidth *= scale;
    // The shoulder cap bridges the coat shoulder and the sleeve top; both of
    // those widen with bulk, so the cap widens with them or a heavy build
    // leaves a gap at the shoulder line.
    p.shoulderScale *= scale;
    p.pelvisThickness *= scale;
    p.torsoLowerThickness *= scale;
    p.torsoUpperThickness *= scale;
    p.coatThickness *= scale;
    p.beltThickness *= scale;
}

}  // namespace uniscope::d2
