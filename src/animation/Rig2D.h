/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_ANIMATION_RIG2D_H
#define UNISCOPE_D2_ANIMATION_RIG2D_H

#include "../gfx/GpuBuffer.h"
#include "../gfx/Renderer2D.h"
#include "../gfx/Texture2D.h"
#include "../gfx/UvRect.h"
#include "uniscope/d2/Material2D.h"
#include "uniscope/d2/ShadowCaster.h"
#include "uniscope/d2/Skeleton.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace uniscope::d2 {

// One drawable mesh bound to one bone.
//
// `widthMode` selects how the quad is sized: along the bone (limbs, torsos),
// as a fixed thickness perpendicular to it, or as a head-sized blob centred on
// the joint. `anchor` is where the joint sits inside the quad in -1..1 quad
// space, which is how a hand stays attached while the forearm swings.
enum class RigWidthMode2D : std::uint8_t {
    AlongBone,
    FixedThickness,
    JointBlob
};

struct RigSkinSlot2D {
    int boneIndex = -1;
    std::string frame;
    UvRect uv{};
    float lengthScale = 1.0f;   // quad length as a multiple of bone length
    // Quad thickness as a fraction of its length, when AlongBone. 0.55, not 1.0:
    // a square limb quad is as thick as it is long, so a thigh reads as a slab
    // rather than a leg, and the art that assumes this ratio (the isotropic
    // material sampling correction in CharacterPartAtlas.cpp) samples a
    // rectangle as if it were a square. At or below 0 falls back to 0.55 in
    // RigRenderer2D::draw.
    float widthScale = 0.55f;
    float thickness = 0.0f;     // quad half-thickness when FixedThickness
    float jointRadius = 0.0f;   // quad half-size when JointBlob
    // Quad length as a multiple of bone length when FixedThickness. 1.0 keeps
    // the quad on its own bone; above 1.0 it overhangs past the joint the
    // anchor selects, which is how one mesh (a long coat) covers several
    // bones. AlongBone ignores it in favour of lengthScale.
    float spanScale = 1.0f;
    float anchorX = -1.0f;      // joint position inside the quad
    float anchorY = 0.0f;
    // Width multipliers for the two ends of the quad, turning the rectangle
    // into an isosceles trapezoid: -1 (the pivot/anchor end) and +1 (the far
    // end). 1.0 keeps the old rectangle. Limbs narrow toward the far end so a
    // thigh stops being a slab; a coat narrows its near (hem) end instead.
    float taperNear = 1.0f;     // -x end
    float taperFar = 1.0f;      // +x end
    // Whether the quad is drawn screen-aligned instead of rotated with its bone.
    //
    // For a limb this is never right: the art runs along the bone, so rotating the
    // quad is what keeps a sleeve on an arm that swings. For a blob it depends on
    // how that blob's art was drawn, and the two cases in this rig want opposite
    // answers - a hand and a foot are painted along their bone, while a head is
    // painted upright, because a head is not a rotated limb.
    //
    // Left to the bone's rotation, a head on an upright neck is rotated by ~90
    // degrees: its texture's vertical axis, which the art is authored with running
    // bottom-to-top ("the joint is the base of the skull, so the mass runs upward"),
    // ends up horizontal, and the head is drawn lying on its side. It is invisible
    // on symmetric procedural art and glaring on anything with a face in it, where
    // it also puts the face's own left-right axis along world up instead of along
    // the direction the character is facing.
    bool upright = false;
    int zOrder = 0;
    MaterialKind2D material = MaterialKind2D::Unlit;
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
    float wetness = 0.0f;
    float emissive = 0.0f;
    // Whether this mesh casts a ground-projected shadow. Small or noisy
    // parts (hands, feet, hair) can opt out to keep per-limb shadows clean.
    bool castShadow = true;
    // Set false to skip the slot in every pass (draw, projected shadow,
    // contact anchor) without removing it from the skin, so the flag can be
    // flipped per facing - e.g. a braid that only exists behind the figure
    // and must not hang into the front view. resolve() keeps invisible slots
    // (and their UVs), so flipping back is free.
    bool visible = true;
};

// Resolves a frame name to atlas UVs. Injected so the library stays free of
// the game's atlas type.
using RigFrameResolver2D = std::function<UvRect(const std::string&)>;

// The direction a bone's quad is drawn with, for a given slot kind.
//
// This is the bone's own rotation and nothing else. Mirroring the rig about its
// vertical axis is x -> -x, and that is a reflection: its matrix has determinant
// -1, so no rotation can express it. The temptation is to "apply the flip to the
// angle", i.e. negate the cosine (-cos, sin, which is the angle pi - theta). That
// is a half turn, not a reflection: the joint still mirrors to the other side but
// the quad also turns about its own centre. A symmetric capsule hides the turn,
// which is why it survived this long; art that is not symmetric about the bone
// axis - the boot, whose sole and shaft lie on opposite sides of it - is drawn
// upside down. That was left-facing standing on its soles.
//
// The reflection therefore lives in rigMirrorFactor and is applied to the quad
// geometry (rx -> -rx) in both the body pass and the shadow pass. The direction
// stays the bone's, so a tapered limb still narrows toward its own tip.
//
// `upright` overrides the bone's rotation entirely and draws on the screen's axes,
// which is what art authored upright needs. The quad is NEVER rotated for
// mirroring: cos=-1,sin=0 is a half turn (rx=-ox, ry=-oy in appendSpriteQuad),
// which turns the head upside down when walking left. Mirroring an upright quad
// is a horizontal UV flip instead (see rigMirroredUv); the bone's own rotation
// does not apply, because the art was never drawn along the bone.
inline void rigDrawDirection(
    const BoneTransform2D& transform,
    bool upright,
    float,
    float& outCosAngle,
    float& outSinAngle) {
    if (upright) {
        outCosAngle = 1.0f;
        outSinAngle = 0.0f;
        return;
    }
    outCosAngle = transform.cosAngle;
    outSinAngle = transform.sinAngle;
}

// x -> -x at the quad-geometry level, for bone-aligned slots. Upright slots keep
// 1.0 and flip their UVs instead (rigMirroredUv), because turning their quad
// about its centre would stand the art on its head rather than face it left.
inline float rigMirrorFactor(bool upright, float flip) {
    return upright ? 1.0f : flip;
}

// Horizontal mirror for upright slots. Swapping u0/u1 flips the image
// left-right while leaving top at the top; rotating by (-1, 0) would flip both
// axes and stand the head on its crown. Bone-aligned slots never use this: they
// mirror their quad geometry through rigMirrorFactor instead.
inline UvRect rigMirroredUv(UvRect uv, bool upright, float flip) {
    if (upright && flip < 0.0f) {
        std::swap(uv.u0, uv.u1);
    }
    return uv;
}

// A skin: the ordered list of meshes bound to a skeleton's bones.
class RigSkin2D {
public:
    // Why a slot did not survive a resolve. MissingBone/MissingFrame mean it was
    // dropped; DuplicateSlot is reported but kept (draw order still decides, and
    // a library cannot know whether layering was intentional).
    enum class RejectReason2D : std::uint8_t {
        MissingBone,
        MissingFrame,
        DuplicateSlot
    };

    struct Rejection2D {
        std::string frame;
        int boneIndex = -1;
        RejectReason2D reason = RejectReason2D::MissingFrame;
    };

    // Assembly diagnostics from the last resolve. Headless and copyable, so a
    // tool or test can assert on what the rig actually bound.
    struct BuildReport2D {
        std::vector<Rejection2D> rejected;
        std::size_t acceptedSlots = 0;
        bool clean() const { return rejected.empty(); }
    };

    void addSlot(RigSkinSlot2D slot);
    void clear();

    // Looks up UVs for every slot and drops slots whose bone or frame is
    // missing. Safe to call again after a reload.
    bool resolve(const Skeleton2D& skeleton, const RigFrameResolver2D& resolver);

    // Same as resolve, and fills `report` with every drop and every duplicate.
    // Passing a null report is exactly resolve().
    bool resolveWithReport(const Skeleton2D& skeleton,
                           const RigFrameResolver2D& resolver,
                           BuildReport2D* report);

    // Optional soft blob drawn under the rig. Empty means no shadow.
    void setShadowFrame(std::string frame) { shadowFrame_ = std::move(frame); }
    const UvRect& shadowUv() const { return shadowUv_; }
    bool hasShadow() const { return shadowUv_.u1 > shadowUv_.u0 && shadowUv_.v1 > shadowUv_.v0; }

    const std::vector<RigSkinSlot2D>& slots() const { return slots_; }
    std::size_t slotCount() const { return slots_.size(); }
    bool valid() const { return !slots_.empty(); }

    // Sets the visible flag on every slot bound to `frame` (a frame can be
    // shared by several slots, like the braid's three chain quads). Returns
    // how many slots were updated.
    int setFrameVisible(const std::string& frame, bool visible);

    // Reorders a single slot's draw depth. Used by rigs that need a different
    // back-to-front order per facing (e.g. head-on views draw both arms over
    // the torso). The caller is responsible for calling sortByDrawOrder()
    // afterwards so the slot vector is reordered for rendering.
    void setSlotZOrder(std::size_t index, int zOrder);

    void sortByDrawOrder();

private:
    std::vector<RigSkinSlot2D> slots_;
    std::string shadowFrame_;
    UvRect shadowUv_{};
};

// Parameters for a sun-projected body silhouette.
//
// The rig is a billboard: its screen Y is body height, not a world-space
// position. A shadow is therefore built by taking every drawn quad's height
// above the soles and sliding it along `fall`, the same way a building's
// footprint is extruded by its wall height. The caller owns the sun (see
// WorldRenderer), so this type only describes the projection, never the time
// of day.
struct RigProjectedShadow2D {
    // Viewport-space origin (skeleton root / pelvis), exactly the values passed
    // to RigRenderer2D::DrawParams. Kept separate so the shadow and the body
    // cannot disagree about where the figure is.
    float originClipX = 0.0f;
    float originClipY = 0.0f;
    // Viewport-space Y of the soles. This stays on the floor while the rig
    // origin rides a jump, which is what makes an airborne figure throw a
    // longer shadow from the same footprint.
    float groundClipY = 0.0f;
    float unitScale = 1.0f;
    float flip = 1.0f;

    // Viewport displacement of the shadow per viewport unit of body height.
    // The caller builds this from the frame sun; because SunPath's length is
    // per world unit of caster height, the light's viewport displacement has
    // to be divided by the camera's world-to-viewport height scale:
    //   fall = -lightViewport / viewportUnitY.y * frameShadow.length
    // so length, direction and the zenith fade all come from SunPath.
    float fallX = 0.0f;
    float fallY = 0.0f;

    // A billboard has no world depth, so an east-west throw would collapse each
    // vertical quad to a line. This turns part of the quad's own screen height
    // into thickness perpendicular to the fall, giving the silhouette an edge
    // the shadow map can actually rasterise. 0 disables it.
    float depthSquash = 0.55f;

    // Lateral spread around the foot axis, matching the buildings' sun-driven
    // widthScale. 1 is the identity.
    float widthScale = 1.0f;

    // Drawn height of the figure in viewport units, used to fade the far end of
    // the throw. <= 0 leaves every vertex at full contact strength.
    float figureHeightClip = 0.0f;

    // Stylistic opacity of the silhouette. The frame's sun alpha is already
    // carried by the shadow map's u_shadow_alpha, so this is not the sun
    // strength - it only trims an intrinsically small caster.
    float alpha = 1.0f;

    // Optional hard contact patch at the soles, in viewport units. <= 0 omits
    // it. The patch is anchored to the lower (planted) foot when
    // `contactFrame` names the foot slots; otherwise it stays at the rig
    // origin. `appendProjectedShadowPolygons` includes it for callers that
    // want one combined caster set; the game submits it separately so it can
    // stay crisp after the sun map's blur.
    float contactRadius = 0.0f;

    // Optional frame name of the foot slots. When non-empty and
    // `contactRadius > 0`, the contact patch follows the supporting foot: the
    // matching feet are compared by ankle height (a swing foot's pointed toe
    // must not win), and the patch anchors at the lower foot's sole midpoint.
    // It blends from the two feet's midpoint (level feet) to that foot (a
    // stride). The player passes CharacterSlot::Foot's frame name. Empty keeps
    // the old origin anchor for callers that want it.
    std::string contactFrame;
};

// Appends the posed rig's sun-projected shadow to a world caster set, one
// convex quad per shadow-casting slot plus an optional sole patch. Every vertex
// is weighted by shadowAlphaAlongFall so the far end fades exactly like a
// building's. Does not draw anything and does not need a GL context; headless
// checks can inspect the resulting ShadowCaster2D directly.
void appendProjectedShadowPolygons(
    ShadowCaster2D& caster,
    const Skeleton2D& posed,
    const RigSkin2D& skin,
    const RigProjectedShadow2D& params);

// Appends only the hard sole patch. The game draws this into the shadow map
// after the blur pass, so the patch keeps a crisp edge while the sun-projected
// body silhouette stays soft. `params.contactRadius` must be > 0; the caller
// owns the strength through the contact pass alpha.
void appendRigContactPatch(
    ShadowCaster2D& caster,
    const Skeleton2D& posed,
    const RigSkin2D& skin,
    const RigProjectedShadow2D& params);

// Draws a posed skeleton through the regular sprite batch.
//
// Every mesh is one rotated quad, so a full character costs about fifteen
// quads and batches together with the rest of the scene.
//
// The contact shadow is a separate call, not part of this one, because it comes
// from a different texture. That separation is not tidiness: a shadow drawn from
// here samples whichever texture it was handed, and the caller has two - the body
// atlas and a dedicated soft-shadow blob. Drawing the shadow inline meant the
// shadow-only pass also drew all nineteen body slots, so the figure was submitted
// a second time textured with the shadow's gradient, and the real pass drew a
// third copy of the shadow sampled from the whole body atlas. One character, three
// overlapping figures, and the two extra ones read as a striped smear behind it.
class RigRenderer2D {
public:
    struct DrawParams {
        // Skeleton root position in the renderer's clip space (the same
        // height-normalized space Camera2D::worldToViewport produces).
        float originClipX = 0.0f;
        float originClipY = 0.0f;
        float unitScale = 1.0f;  // clip units per world unit
        float flip = 1.0f;       // -1 mirrors the rig for left-facing poses
        float tintR = 1.0f;
        float tintG = 1.0f;
        float tintB = 1.0f;
        float alpha = 1.0f;
        // Whole-body lean, radians, applied by the RENDERER after posing: the
        // figure is rotated about (originClipX, originClipY) and every limb's
        // own rotation is composed with it. Nothing in the pose, the clips or
        // the skeleton changes, which is the point - a body riding a swell is
        // a place the body is, not a pose it strikes. 0 is upright.
        float roll = 0.0f;
        float detailScale = 0.0f;
        float detailStrength = 0.0f;
        float shadowStrength = 0.0f;
        float shadowRadius = 0.0f;
        // When true, each skin slot that also has castShadow enabled will be
        // marked as MaterialFlags2D::CastShadow, so the renderer emits a
        // ground-projected silhouette shadow for every limb/torso segment.
        bool castShadow = false;
        // Suppresses the per-quad texture-copy shadow while keeping the
        // CastShadow flag itself. The character uses this because its silhouette
        // is submitted as world polygons instead (see appendProjectedShadowPolygons),
        // but the flag still has to skip the shadow-map read or the body samples
        // its own projected shade back onto itself.
        bool noTextureShadow = false;
    };

    // Draws the posed skeleton. Returns the number of quads submitted.
    //
    // Does not draw the contact shadow; see drawShadow.
    int draw(
        Renderer2D& renderer,
        const Skeleton2D& posed,
        const RigSkin2D& skin,
        const Texture2D& texture,
        const DrawParams& params);

    // Draws only the contact shadow, from the shadow texture.
    //
    // `texture` must be the dedicated shadow texture rather than the body atlas.
    // The shadow's UVs cover that whole texture, so pointing it at the body atlas
    // instead stamps a copy of the entire character grid into a small quad.
    //
    // `params.originClipY` is where the blob lands, so the caller lowers it to the
    // feet; the shadow is on the ground and does not rise with a jump the way the
    // rig origin does.
    int drawShadow(
        Renderer2D& renderer,
        const RigSkin2D& skin,
        const Texture2D& texture,
        const DrawParams& params);
};

} // namespace uniscope::d2

#endif
