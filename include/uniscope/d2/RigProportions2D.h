/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RIGPROPORTIONS2D_H
#define UNISCOPE_D2_RIGPROPORTIONS2D_H

namespace uniscope::d2 {

// The numeric proportions of a biped-like 2D rig: bone lengths, limb girths,
// blob ratios and torso thicknesses.
//
// This is the character-independent part of a body definition. A character
// layer owns the *profile* (including which of these values it ships and how
// they are grouped per gender), while the library owns the table and the two
// scaling rules that every retune needs (uniform height, bulk). Nothing here
// knows about art, animation or a specific character.
//
// All lengths are world units; all width/blob fields are ratios against a bone
// length, so a height scale only has to touch the absolute values.
//
// The default-constructed values below are a NEUTRAL cartoon baseline, not a
// female or a male body. Both gender tables (FemaleCharacter::paramsForProfile
// and MaleCharacter::paramsForProfile) override every field explicitly. Do not
// "fix" a default here to make a gender read better: the defaults are pinned by
// tests/RigSpecChecks.cpp with literal values, and the character layer is the
// only place a sexual-dimorphism delta belongs.
struct RigProportions2D {
    // Bone lengths. Tuned to a ~7-head figure: the head group is one seventh
    // of the total height, which is where the old 1/4.5 read as a mascot.
    float pelvis = 0.074f;
    float spineLow = 0.11f;
    float spineMid = 0.106f;
    float chest = 0.096f;
    // Near-zero on purpose: the neck bone carries no flesh, so anything tall
    // here opens a bare gap between the chin and the scarf and the head
    // floats. The skull base sits practically on the chest tip and the chin
    // tucks into the scarf instead.
    float neck = 0.030f;
    float head = 0.205f;
    float clavicle = 0.046f;
    // Arm proportions: upper arm slightly longer than forearm (~1.1:1)
    float upperArm = 0.158f;
    float forearm = 0.142f;
    float hand = 0.056f;
    // Thigh:shin ≈ 1.15:1, both lengthened so the fingertips reach the hip.
    float thigh = 0.236f;
    float shin = 0.206f;
    // Enlarged shoe: longer foot bone so the boot slab reads bigger.
    float foot = 0.10f;
    // Leg bulk: calf deliberately thicker than thigh (stocky lower leg).
    // Quad thickness over bone length. shinWidth > thighWidth is intentional.
    // The far end of each segment is narrowed by the slot taper.
    float thighWidth = 0.74f;
    // Bulky calf belly: reads sturdy over the slimmed thigh.
    float shinWidth = 0.90f;
    // Enlarged boot: wider quad so authored/procedural shoes read bigger, and
    // so the boot top meets the tapered trouser ankle instead of sitting in it
    // as a small block.
    float footWidth = 1.10f;
    float upperArmWidth = 0.80f;
    float forearmWidth = 0.70f;
    // Blob sizes as multiples of the head/hand baseline. These ratios are
    // deliberately NOT scaled by bulk: bulk is body mass, and a heavier build
    // keeps the same head, hands and eyes. See applyRigBulk2D.
    float handScale = 1.10f;
    // Shoulder cap radius as a multiple of the upper-arm blob baseline
    // (bone length * 0.72). The cap is a flattened yoke, not a disc: the
    // shoulder.png art is wide across the shoulder and thin along the arm, so
    // the quad just has to be big enough to hold that shape. Its half-width
    // lands on the sleeve's own half-height (upperArmWidth * 0.5 * boneLength)
    // so the cap spans from the coat edge to the sleeve's outer edge instead
    // of poking past it as a round pad. 0.52 -> ~0.059 radius (0.118 across),
    // just under the sleeve's 0.142 so the cap melts into it.
    float shoulderScale = 0.52f;
    // Full size of the bone baseline now that the head bone itself is short:
    // the head group (hair included) lands at roughly a seventh of the figure.
    float headScale = 1.00f;
    // Hair keeps its ratio to the head, so the fringe and dome stay glued to
    // the skull at the new size.
    float hairScale = 1.22f;
    // Eye size as a multiple of the head baseline.
    float eyeScale = 1.0f;
    // Torso half-thicknesses in world units. They step up from the waist to
    // the chest instead of sitting on one slab, so the coat has a waist.
    float pelvisThickness = 0.074f;
    float torsoLowerThickness = 0.082f;
    float torsoUpperThickness = 0.094f;
    float coatThickness = 0.10f;
    // The belt is a thin band, not a torso segment: at 0.062 it stood 0.124
    // tall - thicker than the lower torso it crosses - and read as a plank.
    float beltThickness = 0.034f;
    // How far past the joint a limb quad reaches, as a multiple of bone
    // length. Just over 1.0 so neighbours overlap instead of notching.
    float limbOverlap = 1.12f;

    // Limb taper: how far each end of a limb quad narrows. 1.0 keeps the
    // authored girth. The near/far pair on adjacent segments must produce
    // matching half-heights at the shared joint (see the continuity formula in
    // FemaleCharacter::paramsForProfile) or the limb visibly steps when it
    // rotates. Defaults reproduce the values previously hardcoded in
    // CharacterBody.cpp::buildSkin, so adding these fields changes nothing.
    // Ratios, not absolute girths: neither height nor bulk scaling touches them.
    float thighTaperFar       = 0.82f;  // thigh tip (knee end)
    float shinTaperNear       = 0.67f;  // shin joint (knee end)
    float shinTaperFar        = 0.66f;  // shin tip (ankle end)
    float upperArmTaperFar    = 0.78f;  // sleeve tip (elbow end)
    float forearmTaperNear    = 0.89f;  // forearm joint (elbow end)
    float forearmTaperFar     = 0.78f;  // forearm tip (wrist end)
    float pelvisTaperNear     = 0.94f;  // pelvis hem (towards the skirt line)
    float torsoLowerTaperNear = 0.94f;  // lower torso hem
};

// Clamps every field into its sane range in place (non-finite becomes the
// default). Returns true when nothing had to move.
bool validateRigProportions2D(RigProportions2D& proportions);

// Uniform height scaling: multiplies every bone length and the absolute torso
// half-thicknesses. The ratio fields (limb widths, blob scales, overlap) are
// left alone on purpose - they are already relative to a bone length, so
// scaling them too would apply the height factor twice.
void scaleRigProportions2D(RigProportions2D& proportions, float scale);

// Bulk scaling: limb girths, torso/coat/belt thicknesses and the shoulder cap
// only. The head, hair, hand and eye blobs stay at their authored size (bulk is
// body mass, not head size). The shoulder cap is the one blob that grows,
// because it bridges the coat shoulder and the sleeve, both of which widen.
void applyRigBulk2D(RigProportions2D& proportions, float scale);

}  // namespace uniscope::d2

#endif  // UNISCOPE_D2_RIGPROPORTIONS2D_H
