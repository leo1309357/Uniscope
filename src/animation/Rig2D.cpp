/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "Rig2D.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace uniscope::d2 {

namespace {

// The exact quad a slot is drawn with, resolved once so the body pass and the
// projected-silhouette pass cannot grow apart. `draw` feeds these values to
// enqueueSpriteRotated; the shadow pass walks the same corners through the sun
// projection.
struct RigQuadPlacement2D {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float halfWidth = 0.0f;
    // Half-height at the quad's two ends. When a slot has no taper both equal
    // halfHeight, which is exactly the old rectangle.
    float halfHeight = 0.0f;
    float halfHeightNear = 0.0f;
    float halfHeightFar = 0.0f;
    float cosAngle = 1.0f;
    float sinAngle = 0.0f;
    // x -> -x applied to the quad's own geometry. A reflection cannot be folded
    // into cosAngle/sinAngle (that map only spans rotations), so left-facing
    // bone-aligned slots carry it here and both passes apply it to rx.
    float mirrorX = 1.0f;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
    bool valid = false;
};

RigQuadPlacement2D rigQuadPlacement(
    const Skeleton2D& posed,
    const RigSkinSlot2D& slot,
    float originClipX,
    float originClipY,
    float unitScale,
    float flip) {
    RigQuadPlacement2D quad;
    if (slot.boneIndex < 0
        || slot.boneIndex >= static_cast<int>(posed.boneCount())) {
        return quad;
    }

    const BoneTransform2D& transform = posed.worldTransform(slot.boneIndex);
    const Bone2D& bone = posed.bone(static_cast<std::size_t>(slot.boneIndex));
    const float safeScale = std::max(unitScale, 0.0001f);
    // The bone's animated scale shortens/lengthens the limb the same way it
    // moves its children: a scaled shin must draw its cuff where the ankle
    // bone actually sits, or the boot drifts off the trouser leg. bone.scaleX
    // is the posed local scale (child origins already multiply by it in
    // Skeleton2D::solve), so folding it in here keeps the quad and the FK
    // chain in agreement.
    const float boneLength = bone.length * bone.scaleX * safeScale;

    rigDrawDirection(
        transform, slot.upright, flip, quad.cosAngle, quad.sinAngle);
    quad.mirrorX = rigMirrorFactor(slot.upright, flip);

    const bool useThickness = slot.lengthScale <= 0.0f && slot.thickness > 0.0f;
    const bool useBlob = slot.lengthScale <= 0.0f && slot.thickness <= 0.0f;

    // `thickness` and `jointRadius` are world units too, and they have to be
    // converted the same way `boneLength` is.
    //
    // They were not, which split the figure into two halves that disagreed
    // about scale. Everything sized from a bone length tracked the camera;
    // everything sized from a thickness or a joint radius stayed the same
    // size on screen at any zoom. Zoomed out, that left a handful of
    // full-size blocks - head, hands, hair, and the width of the torso -
    // hovering in one place while the limbs that should have joined them
    // shrank to nothing between. That is what read as a solid lump balanced
    // on a stick, and it is not a fixed camera: at any zoom where the two
    // halves disagree you get a different wrong shape.
    const float thickness = slot.thickness * safeScale;
    const float jointRadius = slot.jointRadius * safeScale;

    if (useBlob) {
        quad.halfWidth = jointRadius;
        quad.halfHeight = jointRadius;
    } else if (useThickness) {
        quad.halfWidth = boneLength * 0.5f
            * (slot.spanScale > 0.0f ? slot.spanScale : 1.0f);
        quad.halfHeight = thickness;
    } else {
        const float length =
            boneLength * (slot.lengthScale > 0.0f ? slot.lengthScale : 1.0f);
        quad.halfWidth = length * 0.5f;
        quad.halfHeight =
            length * 0.5f * (slot.widthScale > 0.0f ? slot.widthScale : 0.55f);
    }
    if (quad.halfWidth <= 0.0f || quad.halfHeight <= 0.0f) {
        return quad;
    }

    // A taper of 1 on both ends leaves the two half-heights equal, so every
    // slot that never opts in keeps the exact rectangle it drew before.
    quad.halfHeightNear = quad.halfHeight * slot.taperNear;
    quad.halfHeightFar = quad.halfHeight * slot.taperFar;

    quad.centerX = originClipX + transform.x * safeScale * flip;
    quad.centerY = originClipY + transform.y * safeScale;
    quad.pivotX = -slot.anchorX;
    quad.pivotY = -slot.anchorY;
    quad.valid = true;
    return quad;
}

// One corner of a resolved rig quad, in viewport (pre-aspect) space. The body
// pass and the projected-shadow pass must walk the same corners; keeping this
// in one place is what stops the contact anchor from drifting from the foot
// quad it is supposed to follow.
struct RigCorner2D {
    float x = 0.0f;
    float y = 0.0f;
};

constexpr float kRigCornerX[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
constexpr float kRigCornerY[4] = {-1.0f, -1.0f, 1.0f, 1.0f};

RigCorner2D rigQuadCorner(const RigQuadPlacement2D& quad, int corner) {
    // The -x side of the quad is the near (pivot) end and the +x side the far
    // end, so a tapered limb narrows toward its own tip whichever way the bone
    // happens to point on screen.
    const float hh =
        kRigCornerX[corner] < 0.0f ? quad.halfHeightNear : quad.halfHeightFar;
    const float ox = (kRigCornerX[corner] + quad.pivotX) * quad.halfWidth;
    const float oy = (kRigCornerY[corner] + quad.pivotY) * hh;
    const float rx = (ox * quad.cosAngle - oy * quad.sinAngle) * quad.mirrorX;
    const float ry = ox * quad.sinAngle + oy * quad.cosAngle;
    return {quad.centerX + rx, quad.centerY + ry};
}

// Where the hard contact patch should sit. A patch fixed at the rig origin
// reads as an offset during a stride, because the planted foot is not under
// the pelvis. Find the matching feet, select the supporting one by ankle
// height, and anchor at that foot's sole midpoint; when both feet are level,
// center between them so standing still does not put the patch under one boot.
struct RigContactAnchor2D {
    float x = 0.0f;
    bool valid = false;
};

RigContactAnchor2D rigContactAnchor(
    const Skeleton2D& posed,
    const RigSkin2D& skin,
    const RigProjectedShadow2D& params) {
    RigContactAnchor2D anchor;
    if (params.contactFrame.empty()) {
        return anchor;
    }

    float lowestSoleX = 0.0f;
    float lowestHeight = -std::numeric_limits<float>::infinity();
    float secondSoleX = 0.0f;
    float secondHeight = -std::numeric_limits<float>::infinity();
    int count = 0;
    for (const RigSkinSlot2D& slot : skin.slots()) {
        if (!slot.visible || slot.frame != params.contactFrame) {
            continue;
        }
        const RigQuadPlacement2D quad = rigQuadPlacement(
            posed,
            slot,
            params.originClipX,
            params.originClipY,
            params.unitScale,
            params.flip);
        if (!quad.valid) {
            continue;
        }

        RigCorner2D corners[4];
        for (int corner = 0; corner < 4; ++corner) {
            corners[corner] = rigQuadCorner(quad, corner);
        }
        // The sole is the quad's lower edge. Average its two lowest corners so
        // a rotated foot still anchors at the middle of the boot rather than
        // at whichever corner happens to be lowest.
        int lowerA = 0;
        int lowerB = 1;
        if (corners[lowerB].y > corners[lowerA].y) {
            std::swap(lowerA, lowerB);
        }
        for (int corner = 2; corner < 4; ++corner) {
            if (corners[corner].y > corners[lowerA].y) {
                lowerB = lowerA;
                lowerA = corner;
            } else if (corners[corner].y > corners[lowerB].y) {
                lowerB = corner;
            }
        }
        const float soleX = (corners[lowerA].x + corners[lowerB].x) * 0.5f;
        // Select by the ankle (the foot bone's origin), not by the lowest sole
        // corner or the quad center: a swing foot can point its toe below the
        // stance sole, and the quad center also dips as the foot pitches. The
        // ankle height is what identifies the supporting leg. The patch still
        // anchors at that foot's sole midpoint, so a heel strike or push-off
        // stays under the part of the boot that touches the ground.
        const float safeScale = std::max(params.unitScale, 0.0001f);
        const float footHeight =
            posed.worldTransform(slot.boneIndex).y * safeScale;

        if (count == 0 || footHeight > lowestHeight) {
            secondSoleX = lowestSoleX;
            secondHeight = lowestHeight;
            lowestSoleX = soleX;
            lowestHeight = footHeight;
        } else if (count == 1 || footHeight > secondHeight) {
            secondSoleX = soleX;
            secondHeight = footHeight;
        }
        ++count;
    }

    if (count == 0) {
        return anchor;
    }

    anchor.valid = true;
    anchor.x = lowestSoleX;
    if (count >= 2) {
        // Feet level (idle, double support): midpoint. A clear height gap (a
        // stride): the lower foot. Smooth the hand-off so the hard patch does
        // not visibly jump between boots at the threshold.
        const float level = std::max(params.unitScale * 0.015f, 0.001f);
        const float planted =
            std::max(params.unitScale * 0.060f, level + 0.001f);
        const float t = std::clamp(
            (lowestHeight - secondHeight - level) / (planted - level), 0.0f, 1.0f);
        const float blend = t * t * (3.0f - 2.0f * t);
        const float midpoint = (lowestSoleX + secondSoleX) * 0.5f;
        anchor.x = midpoint + (lowestSoleX - midpoint) * blend;
    }
    return anchor;
}

} // namespace

void RigSkin2D::addSlot(RigSkinSlot2D slot) {
    slots_.push_back(std::move(slot));
}

void RigSkin2D::clear() {
    slots_.clear();
}

bool RigSkin2D::resolve(const Skeleton2D& skeleton, const RigFrameResolver2D& resolver) {
    return resolveWithReport(skeleton, resolver, nullptr);
}

bool RigSkin2D::resolveWithReport(
    const Skeleton2D& skeleton,
    const RigFrameResolver2D& resolver,
    BuildReport2D* report) {
    if (report != nullptr) {
        *report = BuildReport2D{};
    }
    if (!resolver) {
        slots_.clear();
        shadowUv_ = UvRect{};
        return false;
    }

    std::vector<RigSkinSlot2D> resolved;
    resolved.reserve(slots_.size());
    for (const RigSkinSlot2D& slot : slots_) {
        if (slot.boneIndex < 0 || slot.boneIndex >= static_cast<int>(skeleton.boneCount())) {
            if (report != nullptr) {
                report->rejected.push_back(Rejection2D{
                    slot.frame, slot.boneIndex, RejectReason2D::MissingBone});
            }
            continue;
        }
        const UvRect uv = resolver(slot.frame);
        if (uv.u1 <= uv.u0 || uv.v1 <= uv.v0) {
            if (report != nullptr) {
                report->rejected.push_back(Rejection2D{
                    slot.frame, slot.boneIndex, RejectReason2D::MissingFrame});
            }
            continue;
        }
        if (report != nullptr) {
            // Two slots on the same bone with the same art stack in draw order;
            // that is legal, but usually an authoring slip, so say so.
            const bool duplicate = std::any_of(
                resolved.begin(), resolved.end(),
                [&](const RigSkinSlot2D& kept) {
                    return kept.boneIndex == slot.boneIndex
                        && kept.frame == slot.frame;
                });
            if (duplicate) {
                report->rejected.push_back(Rejection2D{
                    slot.frame, slot.boneIndex, RejectReason2D::DuplicateSlot});
            }
        }
        RigSkinSlot2D copy = slot;
        copy.uv = uv;
        resolved.push_back(copy);
    }
    slots_.swap(resolved);
    if (report != nullptr) {
        report->acceptedSlots = slots_.size();
    }
    if (!shadowFrame_.empty()) {
        shadowUv_ = resolver(shadowFrame_);
    }
    sortByDrawOrder();
    return !slots_.empty();
}

void RigSkin2D::sortByDrawOrder() {
    std::stable_sort(
        slots_.begin(),
        slots_.end(),
        [](const RigSkinSlot2D& left, const RigSkinSlot2D& right) { return left.zOrder < right.zOrder; });
}

int RigSkin2D::setFrameVisible(const std::string& frame, bool visible) {
    int updated = 0;
    for (RigSkinSlot2D& slot : slots_) {
        if (slot.frame == frame) {
            slot.visible = visible;
            ++updated;
        }
    }
    return updated;
}

void RigSkin2D::setSlotZOrder(std::size_t index, int zOrder) {
    if (index < slots_.size()) {
        slots_[index].zOrder = zOrder;
    }
}

void appendProjectedShadowPolygons(
    ShadowCaster2D& caster,
    const Skeleton2D& posed,
    const RigSkin2D& skin,
    const RigProjectedShadow2D& params) {
    if (!skin.valid() || posed.boneCount() == 0 || params.alpha <= 0.001f) {
        return;
    }

    const float fallLength =
        std::sqrt(params.fallX * params.fallX + params.fallY * params.fallY);
    // Perpendicular used both for the billboard-depth correction and for the
    // sun's lateral spread. Without a throw the direction is arbitrary; use
    // screen-up so the depth correction still does something readable.
    const float perpX = fallLength > 0.0001f ? -params.fallY / fallLength : 0.0f;
    const float perpY = fallLength > 0.0001f ? params.fallX / fallLength : 1.0f;
    const float widthSpread = params.widthScale - 1.0f;
    const float depthSquash = std::max(params.depthSquash, 0.0f);
    const float fadeHeight = std::max(params.figureHeightClip, 0.0f);

    for (const RigSkinSlot2D& slot : skin.slots()) {
        if (!slot.visible || !slot.castShadow) {
            continue;
        }
        const RigQuadPlacement2D quad = rigQuadPlacement(
            posed,
            slot,
            params.originClipX,
            params.originClipY,
            params.unitScale,
            params.flip);
        if (!quad.valid) {
            continue;
        }

        std::vector<ShadowVertex2D> points;
        points.reserve(4);
        for (int corner = 0; corner < 4; ++corner) {
            // Same vertex walk as Renderer2D::appendSpriteQuad, in the viewport
            // (pre-aspect) space the polygon shadow pass consumes.
            const RigCorner2D vertex = rigQuadCorner(quad, corner);
            const float vertexX = vertex.x;
            const float vertexY = vertex.y;

            // The rig's screen Y is body height. A vertex's shadow slides away
            // from the feet in proportion to how far it stands above the soles.
            const float height = std::max(0.0f, vertexY - params.groundClipY);
            // A billboard has no world depth. Reuse its own vertical extent,
            // perpendicular to the throw, so an east-west shadow is a shape
            // rather than the line a flat plane would cast.
            const float depth = (vertexY - quad.centerY) * depthSquash;
            float shadowX = vertexX + params.fallX * height + perpX * depth;
            float shadowY =
                params.groundClipY + params.fallY * height + perpY * depth;

            if (std::fabs(widthSpread) > 0.001f) {
                const float lateral = (shadowX - params.originClipX) * perpX
                    + (shadowY - params.groundClipY) * perpY;
                shadowX += perpX * lateral * widthSpread;
                shadowY += perpY * lateral * widthSpread;
            }

            points.push_back(
                {shadowX,
                 shadowY,
                 shadowAlphaAlongFall(height, fadeHeight)});
        }
        caster.addConvexPolygonWeighted(std::move(points), params.alpha);
    }

    if (params.contactRadius > 0.0f) {
        appendRigContactPatch(caster, posed, skin, params);
    }
}

void appendRigContactPatch(
    ShadowCaster2D& caster,
    const Skeleton2D& posed,
    const RigSkin2D& skin,
    const RigProjectedShadow2D& params) {
    if (params.contactRadius <= 0.0f || params.alpha <= 0.001f
        || !skin.valid() || posed.boneCount() == 0) {
        return;
    }

    // Hard contact patch. It follows the lower (planted) foot when the caller
    // names the foot frame, and centers between level feet. With no frame it
    // keeps the old origin anchor for callers that want it.
    float cx = params.originClipX;
    if (!params.contactFrame.empty()) {
        const RigContactAnchor2D anchor = rigContactAnchor(posed, skin, params);
        if (anchor.valid) {
            cx = anchor.x;
        }
    }
    const float cy = params.groundClipY;
    const float rx = params.contactRadius;
    // A boot footprint is wider across the sole than the old soft blob's
    // 0.45 aspect. Keep it an octagon rather than a diamond: the game draws
    // this patch after the shadow-map blur, so its outline is visible and a
    // four-corner diamond reads as a decal instead of a contact area.
    const float ry = params.contactRadius * 0.55f;
    constexpr int kSegments = 8;
    constexpr float kTau = 6.28318530718f;
    std::vector<ShadowVertex2D> contact;
    contact.reserve(kSegments);
    for (int i = 0; i < kSegments; ++i) {
        const float angle =
            static_cast<float>(i) / static_cast<float>(kSegments) * kTau;
        contact.push_back(
            {cx + std::cos(angle) * rx, cy + std::sin(angle) * ry, 1.0f});
    }
    caster.addConvexPolygonWeighted(std::move(contact), params.alpha);
}

int RigRenderer2D::drawShadow(
    Renderer2D& renderer,
    const RigSkin2D& skin,
    const Texture2D& texture,
    const DrawParams& params) {
    if (!skin.hasShadow() || !texture.valid() || params.shadowStrength <= 0.001f) {
        return 0;
    }

    MaterialParams2D shadow;
    shadow.kind = MaterialKind2D::Unlit;
    // The blob is a flat, ground-aligned decal, not a lit surface: without
    // FlatSurface the shader derives a normal from the blob's own gradient and
    // rims its silhouette, which puts a bright outline under every character.
    shadow.flags = static_cast<MaterialFlags2D>(
        static_cast<int>(MaterialFlags2D::FlatSurface) |
        static_cast<int>(MaterialFlags2D::NoRim));
    const float scale = std::max(params.unitScale, 0.0001f);
    const float radius = params.shadowRadius > 0.0f ? params.shadowRadius : scale * 0.5f;
    if (!renderer.enqueueSpriteRotated(
            params.originClipX,
            params.originClipY,
            radius,
            radius * 0.45f,
            1.0f,
            0.0f,
            texture,
            skin.shadowUv(),
            0.0f,
            0.0f,
            0.0f,
            params.shadowStrength,
            0.0f,
            0.0f,
            0.0f,
            shadow)) {
        return 0;
    }
    return 1;
}

int RigRenderer2D::draw(
    Renderer2D& renderer,
    const Skeleton2D& posed,
    const RigSkin2D& skin,
    const Texture2D& texture,
    const DrawParams& params) {
    if (!skin.valid() || !texture.valid() || posed.boneCount() == 0) {
        return 0;
    }

    const float unitScale = params.unitScale;
    int submitted = 0;

    // The body lean, hoisted: every slot is rotated about the SAME root, so the
    // trigonometry is done once per draw rather than once per limb. A roll also
    // has to compose with each quad's own angle - a leaning body is not leaning
    // limbs, it is leaning limbs turned further - so the two rotations are
    // multiplied here rather than being passed through independently.
    const bool rolling = std::abs(params.roll) > 1.0e-4f;
    const float rollCos = rolling ? std::cos(params.roll) : 1.0f;
    const float rollSin = rolling ? std::sin(params.roll) : 0.0f;

    for (const RigSkinSlot2D& slot : skin.slots()) {
        if (!slot.visible) {
            continue;
        }
        const RigQuadPlacement2D quad = rigQuadPlacement(
            posed, slot, params.originClipX, params.originClipY, unitScale, params.flip);
        if (!quad.valid) {
            continue;
        }
        // Upright quads stay axis-aligned; left-facing mirrors them by flipping
        // U, never by rotating (which would stand the head upside down).
        const UvRect uv = rigMirroredUv(slot.uv, slot.upright, params.flip);

        MaterialParams2D material;
        material.kind = slot.material;
        material.wetness = slot.wetness;
        material.emissive = slot.emissive;
        material.detail.scale = params.detailScale;
        material.detail.strength = params.detailStrength;
        // A stable per-slot offset keeps cloth weave from aligning across limbs.
        material.detail.offsetU = static_cast<float>(slot.boneIndex) * 0.137f;
        material.detail.offsetV = static_cast<float>(slot.boneIndex) * 0.291f;
        if (params.castShadow && slot.castShadow) {
            material.flags = static_cast<MaterialFlags2D>(
                static_cast<int>(material.flags) |
                static_cast<int>(MaterialFlags2D::CastShadow));
        }
        material.noTextureShadow = params.noTextureShadow;

        if (renderer.enqueueSpriteRotated(
                rolling
                    ? params.originClipX
                          + (quad.centerX - params.originClipX) * rollCos
                          - (quad.centerY - params.originClipY) * rollSin
                    : quad.centerX,
                rolling
                    ? params.originClipY
                          + (quad.centerX - params.originClipX) * rollSin
                          + (quad.centerY - params.originClipY) * rollCos
                    : quad.centerY,
                quad.halfWidth,
                quad.halfHeight,
                rolling ? quad.cosAngle * rollCos - quad.sinAngle * rollSin : quad.cosAngle,
                rolling ? quad.sinAngle * rollCos + quad.cosAngle * rollSin : quad.sinAngle,
                texture,
                uv,
                slot.red * params.tintR,
                slot.green * params.tintG,
                slot.blue * params.tintB,
                slot.alpha * params.alpha,
                1.0f,
                1.0f,
                1.0f,
                material,
                quad.pivotX,
                quad.pivotY,
                slot.taperNear,
                slot.taperFar,
                quad.mirrorX)) {
            ++submitted;
        }
    }

    return submitted;
}

} // namespace uniscope::d2

