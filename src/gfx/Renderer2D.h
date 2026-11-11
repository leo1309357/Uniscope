/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_RENDERER2D_H
#define UNISCOPE_D2_GFX_RENDERER2D_H

#include "GpuBuffer.h"
#include "FontRegistry.h"
#include "SdfFont.h"
#include "ShaderLibrary.h"
#include "Texture2D.h"
#include "Texture2DArray.h"
#include "UvRect.h"

#include "uniscope/d2/Material2D.h"
#include "uniscope/d2/RenderTarget.h"
#include "uniscope/d2/ShadowCaster.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace uniscope::d2 {

// How one category of material boundary is drawn.
//
// The numbers used to be three functions in the sprite shader, which meant
// every material a game could ever add had to be written into the library's
// GLSL before it could have a boundary of its own. The table is the drawing
// parameters, so it belongs to the caller that owns the materials; the shader
// keeps the geometry and the lighting, which is the part that is genuinely
// rendering. Category ids index the table directly, so an id is 0..3.
struct EdgeStyleEntry {
    // Band width as a fraction of the tile, measured inward from the
    // boundary; how much the band darkens; how strong its lit lip is.
    float bandWidth = 0.0f;
    float darken = 0.0f;
    float bevel = 0.0f;
    // The base corner radius the category wants, as a fraction of a tile. A
    // corner between two categories takes the average of the two.
    float radius = 0.0f;
    // The hue the boundary picks up from the far side, as a multiplier.
    float tintR = 1.0f;
    float tintG = 1.0f;
    float tintB = 1.0f;
};

// One boundary category per entry, indexed by the category id. Four entries:
// joined, and the three material families a game maps its tiles onto.
inline constexpr int kEdgeStyleTableSize = 4;
struct EdgeStyleTable {
    EdgeStyleEntry entry[kEdgeStyleTableSize];
    // How far the band travels towards the far side's hue.
    float tintStrength = 0.55f;
};

enum class TextAlign {
    Left,
    Center,
    Right
};

struct TextMetrics {
    float width = 0.0f;
    float height = 0.0f;
};

// Per-sprite material parameters forwarded to textured2d_batch.frag.
// Packed into a vec4 vertex attribute: (kind, wetness, emissive, detailLayer),
// plus a detail-map vec4 and the packed flag field. cornerStyle varies terrain
// corner radii without increasing per-vertex attribute bandwidth.
struct MaterialParams2D {
    MaterialKind2D kind = MaterialKind2D::Unlit;
    float wetness = 0.0f;   // 0..1 accumulation, drives the wet-surface response
    float emissive = 0.0f;  // 0..1 additive glow strength for Emissive kind
    std::uint8_t detailLayer = 0; // slice of the bound detail array; was `reserved`
    std::uint8_t cornerStyle = 0; // stable per-tile seed for organic corner profiles
    DetailMap2D detail{};
    MaterialFlags2D flags = MaterialFlags2D::None;
    // How the material boundaries of this quad are drawn, 2 bits each. Bits 0-7
    // are the four side treatments; bits 8-15 are four game::CornerShape values.
    // These tell the shader where to add material edges and where a rounded
    // diagonal fill is needed. A side of 0 means the two tiles are the same
    // material. A non-zero side is the treatment of the BOUNDARY, not of the
    // neighbour: the two quads sharing a boundary carry the same value, so they
    // draw the same band.
    //
    // Ground quads use this to keep a volume read at terrain transitions while
    // staying seamless everywhere else; the shape bits let the shader round
    // isolated material corners using a tile-stable organic radius.
    //
    // Packed into the high bits of the same float attribute as `flags` rather
    // than a new vertex slot, so it costs no extra attribute bandwidth. Flags
    // use bits 0-1, cornerStyle uses bits 2-7, and edgeMask uses bits 8-23;
    // a float represents this complete 24-bit value exactly.
    std::uint16_t edgeMask = 0;
    // Optional straight-boundary crossfade. This travels in its own vec4
    // attribute; none of the packed flag, seed, side or corner bits change.
    TransitionParams2D transition{};
    // How much of this sprite is under casters this frame, 0..1. Only the
    // per-part material response reads it: a receiver that is one material all
    // over (a ground tile, a rig limb) is shaded by the shadow map instead and
    // leaves this at 0. It exists because a facade is NOT one material - a
    // wall takes the whole shadow and a window keeps the sky it mirrors - so
    // the amount of shade has to arrive with the sprite rather than be
    // flattened into its tint. See MaterialKind2D::BuildingSurface.
    float occluded = 0.0f;
    // CPU-only hint: when CastShadow is also set, skip the renderer's automatic
    // texture-shadow copy. The shader still sees CastShadow and will not sample
    // the screen-space shadow map for this sprite, preventing self-shadowing.
    bool noTextureShadow = false;
};

class Renderer2D {
public:
    Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    void releaseDeviceResources();
    void restoreDeviceResources();
    void preloadCjkFont();

    // ---- Text faces ---------------------------------------------------------
    //
    // Every face the font manifest (fonts/fonts.tsv) describes can be baked,
    // and drawing picks a face per codepoint: the active script's chain first,
    // the Latin chain next, then any baked face that happens to hold the
    // glyph. That last step is what lets a font with an incomplete Chinese set
    // (a Japanese-derived face, say) still be the primary one: the few
    // characters it lacks come from the next face in its chain, at the same
    // position on the line.
    //
    // `script` here is the writing system of the TEXT, not the UI language:
    // running the game in Japanese still sets Latin words inside a Japanese
    // sentence in the Japanese face, because that face carries a Latin set
    // drawn to sit next to its kana.
    void setTextScript(FontScript script);
    FontScript textScript() const { return textScript_; }
    // Which slot of the typographic hierarchy the next draws use. Body by
    // default; a caller that wants the display weight opts in per draw.
    void setTextRole(FontRole role);
    FontRole textRole() const { return textRole_; }

    // The strings the UI can show before any content is loaded (the options
    // menu's language names, today). When a CJK character has to be drawn
    // before the loading screen ever baked - the menu can switch language
    // first - the lazy bake uses this set instead of the thousand-glyph default
    // slice, so the menu does not stall for a second to draw three words.
    void setUiFallbackTexts(std::vector<std::string> texts);

    // Bakes the script's body face from the UI fallback set when it is not
    // baked yet, and reports whether a face of that script now exists.
    //
    // The options menu calls this the moment the player picks a language: the
    // section is the one screen that can be reached before any world load, and
    // its labels are the first CJK the game ever draws. Doing the bake here
    // keeps it out of the draw call, where it would stall the frame.
    bool preloadScriptFace(FontScript script);

    // Read fonts/fonts.tsv and register every face. Returns false when the
    // manifest is missing, in which case the legacy two-family fallback still
    // applies.
    bool loadFontManifest();

    // Bake the whole chain of (script, role): the primary for all the text,
    // each fallback only for the characters the ones before it could not
    // supply. Returns the ids that baked, primary first.
    std::vector<std::string> bakeFaces(
        FontScript script,
        FontRole role,
        const std::vector<std::string>& codepointTexts,
        std::vector<SdfFontLoadReport>* reports = nullptr);

    // Bake the body chain of every script, so switching language later needs
    // no rebake. This is what the loading screen calls. Separate text sets per
    // script: the Chinese face is not asked to bake Japanese kana and the
    // Japanese face is not asked for Han it will never draw.
    bool bakeAllScripts(
        const std::vector<std::string>& latinTexts,
        const std::vector<std::string>& chineseTexts,
        const std::vector<std::string>& japaneseTexts);

    std::size_t bakedFaceCount() const;
    std::vector<std::string> bakedFaceIds() const;
    // Characters of `texts` that no baked face of the script can draw. Empty
    // means the script's text is fully covered.
    std::vector<std::uint32_t> missingGlyphs(
        FontScript script,
        const std::vector<std::string>& texts) const;

    // Bake the CJK atlas for exactly the characters the loaded content uses.
    //
    // The fixed slice this replaces (the first 1200 codepoints from U+4E00) is
    // a guess about which Chinese characters a game might use, and it is a bad
    // one: it slices by codepoint rather than by use, so it covered 19% of the
    // characters the game's own dialogue is written in. The content knows its
    // own characters, and asking it is both cheaper (a few hundred glyphs
    // instead of a thousand) and correct.
    //
    // Returns the report so the caller can say out loud which characters did
    // not fit the atlas - those render as blanks.
    bool bakeCjkFont(
        const std::vector<std::string>& codepointTexts,
        SdfFontLoadReport* report = nullptr);
    // Whether the CJK atlas holds a codepoint. A codepoint it does not hold
    // renders as a blank, so this is the question a content pipeline asks
    // before it trusts its text.
    bool hasCjkGlyph(std::uint32_t codepoint) const;
    // Whether the CJK atlas already holds every character of these texts.
    //
    // The bake is the expensive part of a load - it rasterises the signed
    // distance field of every glyph, which is seconds of main-thread work for
    // a few hundred Chinese characters. A second load of the same content
    // does not need it: the atlas it would produce is the one already in
    // memory. This asks whether that is the case.
    //
    // False whenever the atlas is not content-driven (the default slice is a
    // guess, and the content knows better) or is missing a character.
    bool cjkFontCovers(const std::vector<std::string>& codepointTexts) const;
    // Bumped whenever a font atlas (re)loads. Text-measurement caches key on
    // it so entries computed before a lazy CJK load can never go stale.
    std::uint64_t fontEpoch() const { return fontEpoch_; }
    bool hasDeviceResources() const;
    bool hasSpriteBatch() const;
    bool hasRectBatch() const;
    bool hasSdfTextBatch() const;
    void setEffectQuality(float quality01);
    float effectQuality() const { return effectQuality_; }
    // Applies one whole quality tier at once: the shader effect scalar, the
    // shadow-map resolution scale, bicubic upscaling and the tier name the
    // texture sampler reads. The older setEffectQuality() stays for callers and
    // regression tests that drive only the shader scalar, and it leaves the
    // other preset knobs where setQualityPreset() left them.
    void setQualityPreset(TextureQuality quality);
    TextureQuality qualityTier() const { return qualityTier_; }
    // Automatic quality-tier sliding: when enabled, updateDynamicResolution()
    // steps the tier down under a sustained frame miss and back up once the
    // frame budget is comfortably met (and the cheaper render-scale lever is
    // already maxed). Off by default; setQualityPreset() stays the manual
    // authority and only sets the tier auto mode slides away from.
    void setQualityAuto(bool enabled) { qualityAuto_ = enabled; }
    bool qualityAuto() const { return qualityAuto_; }
    // Multisample sample count for the offscreen world target; 0 disables MSAA.
    // The target is rebuilt on the next beginDynamicScene() when this changes.
    void setSceneMultisample(int samples);
    // Stamps the shader-time uniform source (seconds) directly, replacing the
    // internal frame-index clock for this frame. beginFrame() resets the clock,
    // so a caller that sets a real timestamp does so after beginFrame().
    void setShaderTime(float seconds);
    void setMaterialLightDirection(float x, float y);
    // The boundary treatment table the sprite shader reads, uploaded once per
    // frame before drawing, like every other global lighting quantity. The
    // caller owns the materials, so the caller owns this table; the library's
    // default draws no bands at all, which keeps a library-only user from
    // acquiring a game it did not ask for.
    void setEdgeStyleTable(const EdgeStyleTable& table);
    // 0 at the darkest hour, 1 in full sun. Only the per-part building
    // response reads it (it drives the glass lamp), but it is set once per
    // frame by the world renderer rather than per sprite, like every other
    // lighting quantity here.
    void setDaylightFactor(float daylight01);
    // Ambient light colour and intensity. The colour is multiplied by the
    // intensity and added to the lit result, so (1,1,1,0) matches the old
    // un-ambient path and values above 1.0 can be used to brighten overcast
    // or night scenes. Default is warm neutral at low intensity.
    void setAmbientLight(float red, float green, float blue, float intensity);
    // Toggle ground-projected sprite shadows. When enabled, every sprite with
    // MaterialFlags2D::CastShadow emits a darkened copy offset along the
    // material light direction. Disabled by default for compatibility.
    void setShadowEnabled(bool enabled);
    // Shadow tint and how far the shadow is thrown, in multiples of the
    // caster's half-height. A length of 0.0 disables the offset.
    void setShadowColor(float red, float green, float blue, float alpha);
    void setShadowLength(float length);
    // Lateral shadow distortion for oblique sun (see SunPath::widthScale).
    // Widens the projected silhouette; 1.0 disables.
    void setShadowWidthScale(float scale);
    // Create or resize the internal half-resolution shadow buffer. Called once
    // per frame before drawing shadow-casting sprites. Returns false if the
    // GL context is not ready or the size is invalid, in which case shadows
    // fall back to the old direct-quad path.
    bool ensureShadowBuffers(int framebufferWidth, int framebufferHeight);
    // Replaces any previously submitted geometric shadow casters. The set
    // applies to EVERY flush of the frame (set it once before submitting
    // sprites, not per flush), and the caller must keep the object alive and
    // unmutated until the frame's last flushSpriteBatch(). Pass nullptr to
    // disable polygon shadows for the current frame.
    void setShadowCasters(const ShadowCaster2D* casters);
    // Hard contact casters are rasterised into the same shadow map after the
    // sun pass and, when the sun is enabled, after its blur passes, so their
    // edge stays crisp instead of inheriting the soft sun throw. They use the
    // same MAX blend and screen-space vertices as the sun casters; the separate
    // alpha lets a contact patch survive noon, when the sun's own throw has
    // collapsed. The pointer lifetime rule is the same as setShadowCasters().
    void setContactShadowCasters(const ShadowCaster2D* casters);
    void setContactShadowAlpha(float alpha);
    // 2D heightfield ambient occlusion scale for textured2d_batch.frag.
    // 1.0 keeps the tuned look; 0.0 disables it.
    void setAmbientOcclusionStrength(float strength01);
    // Subsurface scattering for translucent materials (foliage, cloth).
    void setSubsurfaceStrength(float strength01);
    void setSubsurfaceColor(float red, float green, float blue);
    // Environment fog for the sprite batch. Density 0 disables. The center is
    // the camera focus in vAnchor units (clip x, viewport y); the game updates
    // it per frame from weather, biome and time of day.
    void setFog(float red, float green, float blue, float density);
    void setFogCenter(float x, float y);
    // Viewport units per world unit along each axis. The shader divides the
    // aspect-corrected fragment delta by this scale, so fog distance is measured
    // in WORLD units and the falloff stays pinned to the world when the camera
    // zooms. The default (1,1) keeps the legacy behaviour where density is
    // authored against viewport distance, so existing callers render unchanged.
    void setFogWorldScale(float viewportPerWorldX, float viewportPerWorldY);
    // Gameplay vignette for the sprite batch: gentle inward focus, 0
    // disables. Neutral multiply (no tint) so anomaly grades keep owning
    // screen-edge colour. Defaults to 0 - callers that never set it render
    // exactly as before.
    void setVignetteStrength(float strength01);
    // Additive blending for the sprite and rect batches. Flushes any active
    // batch first, so glows, flashes and particles can be submitted in their
    // own pass and the caller restores alpha blending the same way afterwards.
    void setSpriteAdditive(bool additive);
    bool spriteAdditive() const { return spriteAdditive_; }
    // Dynamic resolution (2D super-sampling upscale). In auto mode the render
    // scale tracks a frame-time average between minScale and 1.0; in manual
    // mode fixedScale is used verbatim. beginDynamicScene returns false when
    // the effective scale is ~1.0 (render direct, no offscreen pass).
    void setDynamicResolution(bool autoMode, float fixedScale, float minScale);
    void updateDynamicResolution(float deltaSeconds);
    float renderScale() const { return dynamicScale_; }
    bool dynamicSceneActive() const { return dynamicSceneActive_; }
    bool beginDynamicScene(int framebufferWidth, int framebufferHeight);
    void endDynamicSceneAndPresent(float sharpness);
    // Binds a companion tangent-space normal map that the sprite batch samples
    // for per-texel detail lighting. Pass nullptr to disable (the shader then
    // falls back to its derivative-based detail path). The texture must stay
    // alive and share the albedo atlas UV layout for the duration of the batch.
    void setSpriteNormalTexture(const Texture2D* normalTexture);
    // Pairs one sprite texture with the per-part material mask the batch binds
    // for it (see MaterialKind2D::BuildingSurface). The pair is keyed by the
    // albedo's GL id, so every flush that draws that texture binds the right
    // mask without the caller having to know when a flush happens - the batch
    // breaks on texture change on its own, so this costs one lookup per draw
    // call. Register again whenever either texture is re-uploaded. Only a
    // BuildingSurface sprite ever samples a mask, and such a sprite is always
    // registered in the same frame it is drawn, so an entry left behind by a
    // texture that has since been destroyed is never read. The whole map is
    // dropped on device loss, where every id in it dies anyway.
    // Passing nullptr clears the pairing.
    void registerSpriteMaterialMask(const Texture2D* albedo, const Texture2D* mask);
    // Binds a stack of seamlessly tiling detail layers used for world-space
    // detail mapping (see MaterialParams2D::detail). Pass nullptr to disable,
    // in which case the shader falls back to the atlas normal map or its own
    // derivative-based detail. The array must stay alive for the duration of the
    // batch. Each layer is sampled with its own GL_REPEAT wrap, so a material's
    // detail never bleeds into the next layer's.
    void setSpriteDetailTexture(const Texture2DArray* detailTexture);
    // Binds the building elevation texture array. Each building elevation is one
    // layer in the array. The sprite batch samples from this array when a
    // sprite's BatchVertex::buildingArrayLayer >= 0. Pass nullptr to disable.
    void setBuildingTextureArray(const Texture2DArray* textureArray);
    // Sets the current building array layer index for subsequent sprite submissions.
    // -1 disables array sampling (default). Must be called before submitting
    // building sprites that use the array texture.
    void setBuildingArrayLayer(int layer);
    // Submits a sprite quad using the currently bound building texture array.
    // The layer index is taken from the current building array layer (set via
    // setBuildingArrayLayer). This avoids texture switches between buildings.
    bool enqueueBuildingArraySprite(
        float x, float y, float halfWidth, float halfHeight,
        UvRect uv, float red, float green, float blue, float alpha,
        float blendRed, float blendGreen, float blendBlue,
        float blendLeft, float blendRight, float blendDown, float blendUp,
        MaterialParams2D material, UvRect nextFrameUv, float frameBlend,
        int uvOrientation);
    // Anchor for the world-space detail layers, in the same clip space the
    // detail is sampled in (clip x, viewport y).
    //
    // The detail field is sampled from the sprite's clip position, which is
    // what makes it continuous across neighbouring quads. On its own that
    // anchors the field to the CAMERA, though: the detail then slides over the
    // ground as the camera moves, and a ground whose micro-texture crawls
    // under the player reads as flicker. Subtracting an origin makes the
    // field a function of world position instead.
    //
    // Pass the clip position of the world origin and the detail is fixed to
    // the world; pass the clip position of a character's origin and that
    // character's detail is fixed to the character. Defaults to (0,0), which
    // is exactly the camera-anchored behaviour, so a caller that never sets
    // it renders as before.
    void setDetailOrigin(float x, float y);
    void drawMenuBackground(
        float framebufferAspect,
        float backgroundR,
        float backgroundG,
        float backgroundB,
        float accentR,
        float accentG,
        float accentB,
        float panelR,
        float panelG,
        float panelB);

    // The white-out: a light that starts at the centre of the screen and
    // pushes the darkness out to the edges.
    //
    // `radius` is where the light's edge is, in the screen's half-height
    // units (1.0 is the top edge, 0 is the centre). `intensity` is how
    // bright it is: past 1.0 the middle is blown out to pure white, which is
    // what an overexposed morning looks like.
    // The cinematic backdrop: the void behind a story animation.
    //
    // A cold gradient with slow drifting fog banks and a weak halo behind the
    // centre. It is deliberately NOT the menu background: that one draws a
    // perspective grid and a scan line, which read as the game's front room.
    void drawCinematicBackdrop(
        float framebufferAspect,
        float deepR,
        float deepG,
        float deepB,
        float hazeR,
        float hazeG,
        float hazeB,
        float glowR,
        float glowG,
        float glowB);

    void drawWhiteOut(float framebufferAspect, float radius, float intensity);

    void beginFrame();
    void endFrame();

    void beginRectBatch(float framebufferAspect);
    void flushRectBatch();
    // Batch submit counters for perf regression tests: incremented only on
    // real GPU submissions (not on empty no-op flushes), reset by beginFrame().
    std::uint64_t rectBatchFlushes() const { return rectBatchFlushes_; }
    std::uint64_t sdfBatchFlushes() const { return sdfBatchFlushes_; }
    // The sprite batch is where every world sprite - buildings included -
    // goes out, so this is the number the building-texture work measures: how
    // many times a frame's sprites could not share a draw call. The buildings
    // are one texture per (type, material, open, day/night, snow) variant, so
    // their share of this counter is what an atlas or a texture array would
    // have to drive down.
    std::uint64_t spriteBatchFlushes() const { return spriteBatchFlushes_; }
    // Suspended SDF text batch: between beginTextBatch() and endTextBatch(),
    // drawText()/drawTextAligned()/drawTextInWidth() only enqueue glyphs and
    // skip their usual per-string flush, so a whole HUD pass submits in one
    // (or few, on font switches) draw call. endTextBatch() flushes once.
    // No rect/sprite/scissor emission is allowed inside the scope: immediate
    // rects would draw before the still-queued glyphs and break paint order.
    void beginTextBatch(float framebufferAspect);
    void endTextBatch();

    void setScissorRect(int x, int y, int width, int height);
    void clearScissor();

    void drawSquare(float x, float y, float halfSize, float framebufferAspect, float red, float green, float blue);
    void drawRect(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        float red,
        float green,
        float blue);
    void drawRectAlpha(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        float red,
        float green,
        float blue,
        float alpha);
    void drawTexturedRect(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        const Texture2D& texture,
        float red = 1.0f,
        float green = 1.0f,
        float blue = 1.0f,
        float alpha = 1.0f);
    bool captureBackbufferForBlur(int framebufferWidth, int framebufferHeight);
    bool drawCapturedBlurredRect(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        int framebufferWidth,
        int framebufferHeight,
        float red,
        float green,
        float blue,
        float alpha,
        float radiusPixels = 6.0f);
    bool drawSprite(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        const Texture2D& texture,
        UvRect uv,
        float red = 1.0f,
        float green = 1.0f,
        float blue = 1.0f,
        float alpha = 1.0f);

    void beginSpriteBatch(float framebufferAspect);
    bool enqueueSprite(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        const Texture2D& texture,
        UvRect uv,
        float red = 1.0f,
        float green = 1.0f,
        float blue = 1.0f,
        float alpha = 1.0f,
        float blendRed = 1.0f,
        float blendGreen = 1.0f,
        float blendBlue = 1.0f,
        float blendLeft = 0.0f,
        float blendRight = 0.0f,
        float blendDown = 0.0f,
        float blendUp = 0.0f,
        MaterialParams2D material = {},
        UvRect nextFrameUv = {},
        float frameBlend = 0.0f,
        int uvOrientation = 0);
    // Same as enqueueSprite but rotates the quad about its center. Limb and
    // prop meshes need this because the sprite batch is otherwise axis
    // aligned. vLocal still reports the unrotated corner, so edge blending and
    // the geometric normal follow the sprite's own frame.
    bool enqueueSpriteRotated(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float cosAngle,
        float sinAngle,
        const Texture2D& texture,
        UvRect uv,
        float red = 1.0f,
        float green = 1.0f,
        float blue = 1.0f,
        float alpha = 1.0f,
        float blendRed = 1.0f,
        float blendGreen = 1.0f,
        float blendBlue = 1.0f,
        MaterialParams2D material = {},
        float pivotX = 0.0f,
        float pivotY = 0.0f,
        float taperNear = 1.0f,
        float taperFar = 1.0f,
        // -1 reflects the quad about its own vertical axis (x -> -x). Mirroring a
        // rig for a left-facing pose is a reflection, not a rotation, so it cannot
        // be passed as a negated cosAngle: that is a half turn and draws asymmetric
        // art (a boot's sole and shaft) upside down. Applied before shear, which is
        // a screen-space light offset and stays unmirrored.
        float mirrorX = 1.0f);
    void flushSpriteBatch();
    // Marks the cached render target stale. Called by everything in this class
    // that binds a framebuffer (the dynamic-resolution target, the shadow
    // passes) and by beginFrame(), because a frame may draw into a different
    // destination than the one before it.
    void noteRenderTargetChanged() { renderTargetKnown_ = false; }
    // Diagnostic hook (used by regression tests): queued but unflushed
    // sprite vertex count. A CastShadow sprite with the noTextureShadow hint
    // queues 6 vertices (sprite only); without the hint it queues 12
    // (sprite plus its automatic texture-shadow copy).
    std::size_t spriteBatchQueued() const { return spriteBatch_.size(); }

    bool enqueueRect(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        float red,
        float green,
        float blue,
        float alpha = 1.0f);
    bool enqueueClipQuad(
        float x0,
        float y0,
        float x1,
        float y1,
        float x2,
        float y2,
        float x3,
        float y3,
        float framebufferAspect,
        float red,
        float green,
        float blue,
        float alpha = 1.0f);

    void drawText(
        const char* text,
        float x,
        float y,
        float scale,
        float framebufferAspect,
        float red,
        float green,
        float blue,
        float alpha = 1.0f);
    void drawTextAtBaseline(
        const char* text,
        float x,
        float baselineY,
        float scale,
        float framebufferAspect,
        float red,
        float green,
        float blue);
    void drawTextAligned(
        const std::string& text,
        float anchorX,
        float y,
        float scale,
        TextAlign align,
        float framebufferAspect,
        float red,
        float green,
        float blue);
    void drawTextInWidth(
        const std::string& text,
        float anchorX,
        float y,
        float preferredScale,
        float maxWidth,
        TextAlign align,
        float framebufferAspect,
        float red,
        float green,
        float blue);
    TextMetrics measureText(const std::string& text, float scale) const;
    float fitTextScale(const std::string& text, float preferredScale, float maxWidth) const;
    float estimateTextWidth(const std::string& text, float uiScale) const;
    float textAscent(float uiScale) const;

private:
    // One baked face plus where it sits in the manifest.
    struct LoadedFace {
        SdfFont font;
        FontScript script = FontScript::Latin;
        FontRole role = FontRole::Body;
        int order = 0;
        std::string id;
        bool contentDriven = false;
    };

    bool hasAnySdfFont() const;
    const SdfFont& fontForCodepoint(std::uint32_t codepoint) const;
    float uiScaleToSdfScale(float uiScale) const;
    float uiScaleToSdfScale(float uiScale, const SdfFont& font) const;
    bool drawTexturedQuad(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        const Texture2D& texture,
        UvRect uv,
        float red,
        float green,
        float blue,
        float alpha);

    void initializeSquareMesh();
    void initializeTexturedMesh();
    void initializeBatchMesh();
    void appendClipRectQuad(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float red,
        float green,
        float blue,
        float alpha);
    void appendClipViewportQuad(
        float x0,
        float y0,
        float x1,
        float y1,
        float x2,
        float y2,
        float x3,
        float y3,
        float red,
        float green,
        float blue,
        float alpha);
    bool enqueueRectQuad(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        float framebufferAspect,
        float red,
        float green,
        float blue,
        float alpha);
    bool enqueueClipQuadInternal(
        float x0,
        float y0,
        float x1,
        float y1,
        float x2,
        float y2,
        float x3,
        float y3,
        float framebufferAspect,
        float red,
        float green,
        float blue,
        float alpha);
    void appendSpriteQuad(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        UvRect uv,
        float red,
        float green,
        float blue,
        float alpha,
        float blendRed = 1.0f,
        float blendGreen = 1.0f,
        float blendBlue = 1.0f,
        float blendLeft = 0.0f,
        float blendRight = 0.0f,
        float blendDown = 0.0f,
        float blendUp = 0.0f,
        MaterialParams2D material = {},
        UvRect nextFrameUv = {},
        float frameBlend = 0.0f,
        float cosAngle = 1.0f,
        float sinAngle = 0.0f,
        float pivotX = 0.0f,
        float pivotY = 0.0f,
        float shearX = 0.0f,
        float shearY = 0.0f,
        int uvOrientation = 0,
        float taperNear = 1.0f,
        float taperFar = 1.0f,
        float mirrorX = 1.0f);
    void appendSdfQuad(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        UvRect uv,
        float red,
        float green,
        float blue,
        float alpha);
    void beginSdfTextBatch(float framebufferAspect);
    bool enqueueSdfGlyph(
        float x,
        float y,
        float halfWidth,
        float halfHeight,
        UvRect uv,
        float red,
        float green,
        float blue,
        float alpha);
    void flushSdfTextBatch();
    // Grows the batch store to hold at least vertexCount vertices. SubData past
    // the end of the store is a GL_INVALID_VALUE that leaves the draw reading
    // whatever the previous submission left there, so every flush that only
    // fills the store has to ask first.
    void ensureBatchBuffer(std::size_t vertexCount);
    void initializeSdfFont();
    void ensureCjkFont();
    // Bakes the default-slice body face of one script when none exists. With
    // `useUiTexts`, the bake is content-driven on the UI fallback set instead
    // of the thousand-glyph slice - the on-demand path for a menu that has to
    // draw a CJK word before any content load.
    void ensureScriptFont(FontScript script, bool useUiTexts);
    // Reads the manifest (once) and bakes the Latin body face, which every text
    // measurement is expressed in. Cheap and idempotent; recipe-free callers
    // (text draw, CJK catch-up) go through it before asking for a face.
    void ensureFontFaces();
    // Bakes (or re-bakes) one face of a chain. `texts` already excludes the
    // characters earlier faces supplied.
    bool bakeScriptFace(
        FontScript script,
        FontRole role,
        const std::vector<std::string>& texts,
        bool contentDriven);
    bool bakeFace(
        const FontFace& face,
        const std::vector<std::string>& texts,
        bool contentDriven);
    // The baked face with that manifest id, or nullptr.
    const LoadedFace* loadedFace(const std::string& id) const;
    // The baked face a script's body chain starts with, or nullptr. Used for
    // metrics and for the legacy CJK queries.
    const SdfFont* firstBakedFace(FontScript script, FontRole role) const;
    const SdfFont* metricsFont() const;
    // True once any face of a script was baked from a content's own characters
    // rather than from the default slice.
    bool faceIsContentDriven(FontScript script) const;
    bool ensureBackdropTexture(int framebufferWidth, int framebufferHeight);
    void releaseShadowBuffers();
    // incremental=true draws the current batch into the raw accumulation map
    // WITHOUT clearing it (MAX-blend makes the append exact) and skips the
    // polygon pass, which is already in the map from this frame's full render;
    // false clears and redraws everything.
    void renderShadowMapPass(bool incremental);
    void renderPolygonShadowPass();
    void renderContactShadowPass();
    void blurShadowMap();
    void initializePolygonShadowMesh();

    ShaderLibrary shaders_;
    std::unique_ptr<VertexArray> squareVao_;
    std::unique_ptr<VertexBuffer> squareVbo_;
    std::unique_ptr<VertexArray> texturedVao_;
    std::unique_ptr<VertexBuffer> texturedVbo_;
    std::unique_ptr<VertexArray> batchVao_;
    std::unique_ptr<VertexBuffer> batchVbo_;
    std::unique_ptr<VertexArray> polygonShadowVao_;
    std::unique_ptr<VertexBuffer> polygonShadowVbo_;
    std::unique_ptr<VertexArray> contactShadowVao_;
    std::unique_ptr<VertexBuffer> contactShadowVbo_;

    struct PolygonShadowVertex {
        float x = 0.0f;
        float y = 0.0f;
        float alpha = 1.0f;
    };

    struct BatchVertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
        float localX = 0.0f;
        float localY = 0.0f;
        float blendR = 1.0f;
        float blendG = 1.0f;
        float blendB = 1.0f;
        float blendLeft = 0.0f;
        float blendRight = 0.0f;
        float blendDown = 0.0f;
        float blendUp = 0.0f;
        // Material params (vec4 attribute): kind, wetness, emissive, detailRot.
        float matKind = 0.0f;
        float matWetness = 0.0f;
        float matEmissive = 0.0f;
        float matReserved = 0.0f;
        float nextU = 0.0f;
        float nextV = 0.0f;
        float frameBlend = 0.0f;
        // Detail mapping (vec4 attribute): scale, strength, offsetU, offsetV.
        float detailScale = 0.0f;
        float detailStrength = 0.0f;
        float detailOffsetU = 0.0f;
        float detailOffsetV = 0.0f;
        // How much of this sprite is under casters (MaterialParams2D::occluded).
        // Carried as its own attribute rather than folded into one of the
        // packed floats: every other slot already carries three things at once
        // and a fourth would be read by whoever unpacked it first. Zero for
        // everything but per-part material receivers, which ignore it anyway.
        float occluded = 0.0f;
        // Material flags (float attribute): MaterialFlags2D bitfield.
        float materialFlags = 0.0f;
        // Building texture array layer index. Only used when the sprite samples
        // from the building texture array (u_building_array). -1 means not using array.
        float buildingArrayLayer = -1.0f;
        // TransitionLayer2D, overlay axis / base side mask, weight jitter and
        // absolute world-tile tangent origin. Constant across the quad.
        float transitionMode = 0.0f;
        float transitionAxis = 0.0f;
        float transitionJitter = 0.0f;
        float transitionOrigin = 0.0f;
    };

    std::vector<BatchVertex> spriteBatch_;
    std::vector<BatchVertex> rectBatch_;
    std::vector<BatchVertex> sdfTextBatch_;
    float batchAspect_ = 1.0f;
    float rectBatchAspect_ = 1.0f;
    float sdfBatchAspect_ = 1.0f;
    float sdfBatchScale_ = 1.0f;
    unsigned int batchTextureId_ = 0;
    // Size of the store behind batchVbo_, in bytes. Sprite flushes re-specify
    // the store to what they carry; rect and SDF flushes only fill it, so they
    // have to grow it first when their batch is larger.
    std::size_t batchBufferBytes_ = 0;
    bool batchActive_ = false;
    bool rectBatchActive_ = false;
    bool sdfBatchActive_ = false;
    bool sdfFlushSuspended_ = false;
    std::uint64_t rectBatchFlushes_ = 0;
    std::uint64_t sdfBatchFlushes_ = 0;
    std::uint64_t spriteBatchFlushes_ = 0;
    unsigned int backdropTextureId_ = 0;
    int backdropTextureWidth_ = 0;
    int backdropTextureHeight_ = 0;
    bool backdropCaptured_ = false;
    float shaderTimeSeconds_ = 0.0f;
    std::uint64_t frameIndex_ = 0;
    float effectQuality_ = 1.0f;
    // The boundary treatment table, as last handed over by the caller, and the
    // version stamp that says whether the shader still holds this copy. The
    // table is uploaded at most once per change rather than once per flush: it
    // is the same handful of floats for the whole frame.
    EdgeStyleTable edgeStyleTable_{};
    bool edgeStyleTableDirty_ = true;
    float materialLightDirectionX_ = -0.42f;
    float materialLightDirectionY_ = 0.68f;
    float daylightFactor_ = 1.0f;
    float ambientR_ = 0.08f;
    float ambientG_ = 0.07f;
    float ambientB_ = 0.06f;
    float ambientIntensity_ = 0.35f;
    bool shadowEnabled_ = false;
    float shadowR_ = 0.0f;
    float shadowG_ = 0.0f;
    float shadowB_ = 0.0f;
    float shadowA_ = 0.40f;
    float shadowLength_ = 0.55f;
    float shadowWidthScale_ = 1.0f;
    float aoStrength_ = 1.0f;
    float sssStrength_ = 0.55f;
    float sssColorR_ = 1.0f;
    float sssColorG_ = 0.45f;
    float sssColorB_ = 0.25f;
    float fogR_ = 0.62f;
    float fogG_ = 0.68f;
    float fogB_ = 0.75f;
    float fogDensity_ = 0.0f;
    float fogCenterX_ = 0.0f;
    float fogCenterY_ = 0.0f;
    float fogWorldScaleX_ = 1.0f;
    float fogWorldScaleY_ = 1.0f;
    // Anchor for the world-space detail field, in clip space. (0,0) keeps the
    // field camera-anchored, which is what every caller rendered before the
    // anchor existed.
    float detailOriginX_ = 0.0f;
    float detailOriginY_ = 0.0f;
    float vignetteStrength_ = 0.0f;
    bool spriteAdditive_ = false;
    // Dynamic resolution state. The offscreen target is owned here so its
    // lifetime follows the GL context (released/restored with everything else).
    bool dynamicAuto_ = false;
    float dynamicFixedScale_ = 1.0f;
    float dynamicMinScale_ = 0.5f;
    float dynamicScale_ = 1.0f;
    float dynamicFrameTimeEma_ = 1.0f / 60.0f;
    float dynamicCooldown_ = 0.0f;
    // Quality-tier auto sliding (see setQualityAuto): off by default, with its
    // own longer cooldown - a tier switch rebuilds the shadow buffers, so it
    // must be rarer than the resolution-scale nudges.
    bool qualityAuto_ = false;
    float qualityCooldown_ = 0.0f;
    RenderTarget dynamicTarget_;
    int dynamicScreenWidth_ = 0;
    int dynamicScreenHeight_ = 0;
    bool dynamicSceneActive_ = false;
    // Screen-space shadow buffers. Casters accumulate into shadowRaw_ (cleared
    // only on a full render: repeat flushes in a frame append their batch so a
    // mid-frame texture switch can no longer drop earlier flushes' shadows),
    // the blur reads shadowRaw_ through shadowBlur_ into shadowMap_, and the
    // lit sprite pass samples shadowMap_. Contact patches draw into shadowMap_
    // after the blur so they keep their crisp edge.
    RenderTarget shadowMap_;
    RenderTarget shadowBlur_;
    RenderTarget shadowRaw_;
    int shadowBufferWidth_ = 0;
    int shadowBufferHeight_ = 0;

    // Quality preset (single knob for effectQuality, shadowResolutionScale,
    // bicubicUpscale, texture filter). Defaults to High for existing callers.
    TextureQuality qualityTier_ = TextureQuality::High;

    // Scene multisample count for the offscreen world target. 0 = disabled.
    // Rebuilding the dynamic target happens on the next beginDynamicScene().
    int sceneSamples_ = 0;

    // Optional shader time override (seconds). When set (>0), used instead of
    // the internal frame-index clock for this frame. beginFrame() resets it.
    float shaderTimeOverride_ = -1.0f;

    // Shadow cache: avoids re-rendering the shadow map when the caster set,
    // frame index, and quality-dependent resolution have not changed.
    const ShadowCaster2D* cachedShadowCasters_ = nullptr;
    std::uint64_t cachedShadowFrame_ = 0;
    int cachedShadowWidth_ = 0;
    int cachedShadowHeight_ = 0;
    bool cachedShadowEnabled_ = false;
    // True while the current sprite batch holds ground-projected texture-shadow
    // copies. Those copies only enter the shadow map when the batch flushes, so
    // a flush that carries new ones must re-render the map even when every
    // cache key above matches - otherwise the copies are discarded by the lit
    // pass and the casters silently lose their shadows.
    bool batchHasShadowCopies_ = false;
    const Texture2D* spriteNormalTexture_ = nullptr;
    const Texture2DArray* spriteDetailTexture_ = nullptr;
    // Building texture array: all building elevations packed into one
    // Texture2DArray. Layer index is carried in BatchVertex::buildingArrayLayer.
    const Texture2DArray* buildingTextureArray_ = nullptr;
    // Current building array layer index being submitted. -1 = not using array.
    // Set via setBuildingArrayLayer() before submitting building sprites.
    int currentBuildingArrayLayer_ = -1;
    // Geometric shadow casters submitted by the game (buildings). Not owned.
    const ShadowCaster2D* shadowCasters_ = nullptr;
    // Hard contact casters, drawn after the sun pass (and its blur, when the
    // sun is enabled). Not owned.
    const ShadowCaster2D* contactShadowCasters_ = nullptr;
    float contactShadowAlpha_ = 0.0f;
    // The framebuffer and viewport the sprite batch draws into, cached for the
    // frame. Querying them costs a driver round-trip each time (see
    // flushSpriteBatch), so the renderer keeps the last known values and marks
    // them stale whenever it binds a target of its own. Renderer2D is the only
    // thing in the engine that binds a framebuffer, so nothing else can move it
    // behind its back.
    GLint renderTargetFbo_ = 0;
    GLint renderTargetViewport_[4] = {0, 0, 0, 0};
    bool renderTargetKnown_ = false;
    // Per-part material masks paired with the sprite texture they belong to.
    // Not owned; cleared on device loss so no recycled id can mis-pair.
    std::unordered_map<unsigned int, const Texture2D*> spriteMaterialMasks_;
    std::vector<PolygonShadowVertex> polygonShadowVertices_;
    std::vector<PolygonShadowVertex> contactShadowVertices_;
    // Triangulation cache: the caster set is frozen once submit starts, so
    // every flush in a frame fans identical polygons. Rebuild only when the
    // set pointer changes; beginFrame() clears the key because a new frame
    // may reuse the same stack address for different content.
    const ShadowCaster2D* triangulatedCasters_ = nullptr;
    const ShadowCaster2D* triangulatedContactCasters_ = nullptr;
    // Aspect the cached triangulation was screen-culled for. The cull bakes
    // the viewport width into the kept vertex set, so an aspect change with
    // the same caster pointer must rebuild.
    float triangulatedCastersAspect_ = -1.0f;
    float triangulatedContactCastersAspect_ = -1.0f;
    static constexpr std::size_t kMaxBatchVertices = 24 * 1024;
    static constexpr std::size_t kMaxContactShadowVertices = 1024;

    // Heap-owned so a later bake cannot move an SdfFont out from under a
    // pointer the text batch is holding.
    std::vector<std::unique_ptr<LoadedFace>> faces_;
    FontScript textScript_ = FontScript::Chinese;
    FontRole textRole_ = FontRole::Body;
    bool manifestLoaded_ = false;
    std::vector<std::string> uiFallbackTexts_;
    // Re-entrancy guard: ensureFontFaces() bakes a face, and the bake path asks
    // ensureFontFaces() again. One level in, the answer is "already being done".
    bool ensureFontFacesBusy_ = false;
    const SdfFont* sdfBatchFont_ = nullptr;
    std::uint64_t fontEpoch_ = 0;
};

} // namespace uniscope::d2

#endif
