/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "Renderer2D.h"

#include "FontAssets.h"
#include "ShaderId.h"
#include "Utf8.h"

#include "GlCompat.h"
#include "PlatformLog.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>

namespace uniscope::d2 {

namespace {
constexpr float kMinimumReadableScale = 0.006f;

void logBatchInitFailure(const char* stage, const std::exception& error) {
    UNISCOPE_D2_LOG_ERROR("Renderer2D: %s failed: %s\n", stage, error.what());
}

bool tryInitializeBatchResources(ShaderLibrary& shaders, std::unique_ptr<VertexArray>& batchVao, std::unique_ptr<VertexBuffer>& batchVbo) {
    try {
        shaders.program(ShaderId::Textured2DBatch);
        shaders.program(ShaderId::Rect2DBatch);
        shaders.program(ShaderId::SdfTextBatch);
        shaders.program(ShaderId::ShadowPolygon);
        batchVao = std::make_unique<VertexArray>();
        batchVbo = std::make_unique<VertexBuffer>();
        return true;
    } catch (const std::exception& error) {
        logBatchInitFailure("batch shader init", error);
        batchVao.reset();
        batchVbo.reset();
        return false;
    }
}

} // namespace

Renderer2D::Renderer2D() {
    restoreDeviceResources();
}

void Renderer2D::releaseDeviceResources() {
    flushSpriteBatch();
    flushRectBatch();
    flushSdfTextBatch();
    faces_.clear();
    sdfBatchFont_ = nullptr;
    squareVbo_.reset();
    squareVao_.reset();
    texturedVbo_.reset();
    texturedVao_.reset();
    batchVbo_.reset();
    batchVao_.reset();


    spriteBatch_.clear();
    rectBatch_.clear();
    batchActive_ = false;
    rectBatchActive_ = false;
    batchBufferBytes_ = 0;
    batchTextureId_ = 0;
    backdropCaptured_ = false;
    dynamicTarget_.destroy();
    dynamicSceneActive_ = false;
    releaseShadowBuffers();
    polygonShadowVao_.reset();
    polygonShadowVbo_.reset();
    polygonShadowVertices_.clear();
    contactShadowVao_.reset();
    contactShadowVbo_.reset();
    contactShadowVertices_.clear();
    shadowCasters_ = nullptr;
    contactShadowCasters_ = nullptr;
    contactShadowAlpha_ = 0.0f;
    // The VBOs above are gone, so their cache keys must not survive a device
    // restore even if a caller sets a caster set before the next beginFrame().
    triangulatedCasters_ = nullptr;
    triangulatedContactCasters_ = nullptr;
    // The masks are keyed by GL id, and every id in this map dies with the
    // context. Keeping them would let a recycled id point a building at another
    // building's materials the moment one is re-uploaded.
    spriteMaterialMasks_.clear();
    if (backdropTextureId_ != 0) {
        glDeleteTextures(1, &backdropTextureId_);
        backdropTextureId_ = 0;
        backdropTextureWidth_ = 0;
        backdropTextureHeight_ = 0;
    }
    shaders_.clear();
}

void Renderer2D::restoreDeviceResources() {
    if (!hasDeviceResources()) {
        try {
            shaders_.program(ShaderId::Rect2D);
            shaders_.program(ShaderId::Textured2D);
        } catch (const std::exception& error) {
            logBatchInitFailure("core shader init", error);
            return;
        }

        squareVao_ = std::make_unique<VertexArray>();
        squareVbo_ = std::make_unique<VertexBuffer>();
        texturedVao_ = std::make_unique<VertexArray>();
        texturedVbo_ = std::make_unique<VertexBuffer>();
        initializeSquareMesh();
        initializeTexturedMesh();
    }

    if (!hasRectBatch() || !hasSdfTextBatch()) {
        batchVao_.reset();
        batchVbo_.reset();
        spriteBatch_.clear();
        rectBatch_.clear();
        sdfTextBatch_.clear();
        batchTextureId_ = 0;
        batchBufferBytes_ = 0;
        batchActive_ = false;
        rectBatchActive_ = false;
        sdfBatchActive_ = false;

        if (tryInitializeBatchResources(shaders_, batchVao_, batchVbo_)) {
            initializeBatchMesh();
            // World frames regularly exceed a few thousand verts (tiles plus
            // entities plus shadow doubles); reserving the full store skips
            // the warmup realloc churn. Rect/SDF traffic stays small, so
            // those keep their modest reserves.
            spriteBatch_.reserve(kMaxBatchVertices);
            rectBatch_.reserve(8192);
            sdfTextBatch_.reserve(4096);
        }

        polygonShadowVao_ = std::make_unique<VertexArray>();
        polygonShadowVbo_ = std::make_unique<VertexBuffer>();
        contactShadowVao_ = std::make_unique<VertexArray>();
        contactShadowVbo_ = std::make_unique<VertexBuffer>();
        initializePolygonShadowMesh();
    }

    if (!hasAnySdfFont()) {
        initializeSdfFont();
    }
}

namespace {

// What a draw falls back to when nothing baked. An invalid SdfFont holds no
// glyphs, so the loop lays the string out from the estimate and draws nothing -
// the same failure mode the renderer already had, without a null check at every
// call site.
const SdfFont& emptyFace() {
    static const SdfFont kEmpty;
    return kEmpty;
}

} // namespace

void Renderer2D::ensureFontFaces() {
    if (ensureFontFacesBusy_) {
        return;
    }
    ensureFontFacesBusy_ = true;
    if (!manifestLoaded_) {
        loadFontManifest();
    }
    if (!FontRegistry::instance().empty()
        && firstBakedFace(FontScript::Latin, FontRole::Body) == nullptr) {
        bakeFaces(FontScript::Latin, FontRole::Body, {}, nullptr);
    }
    ensureFontFacesBusy_ = false;
}

void Renderer2D::initializeSdfFont() {
    sdfBatchFont_ = nullptr;
    if (hasAnySdfFont()) {
        return;
    }
    ensureFontFaces();

    if (FontRegistry::instance().empty()) {
        // No manifest beside the binary: keep the pre-manifest behaviour (one
        // Latin family and one CJK family from FontAssets) so a source tree
        // without the fonts/ bundle still draws text.
        auto latin = std::make_unique<LoadedFace>();
        latin->id = "legacy:latin";
        latin->script = FontScript::Latin;
        latin->font.load(FontFamily::Default);
        if (latin->font.valid()) {
            faces_.push_back(std::move(latin));
        } else {
            UNISCOPE_D2_LOG_ERROR("Renderer2D: Default font unavailable, UI text may be missing\n");
        }

        auto cjk = std::make_unique<LoadedFace>();
        cjk->id = "legacy:cjk";
        cjk->script = FontScript::Chinese;
        cjk->font.load(FontFamily::SourceHanSans);
        if (cjk->font.valid()) {
            faces_.push_back(std::move(cjk));
        } else {
            UNISCOPE_D2_LOG_ERROR("Renderer2D: SourceHanSans font unavailable, CJK text may be missing\n");
        }

        if (!faces_.empty()) {
            ++fontEpoch_;
        }
        return;
    }

    if (!hasAnySdfFont()) {
        UNISCOPE_D2_LOG_ERROR("Renderer2D: no font face baked, UI text will be missing\n");
    }
}

void Renderer2D::ensureCjkFont() {
    const FontScript script = textScript_ == FontScript::Latin ? FontScript::Chinese : textScript_;
    ensureScriptFont(script, false);
}

void Renderer2D::ensureScriptFont(FontScript script, bool useUiTexts) {
    if (firstBakedFace(script, FontRole::Body) != nullptr) {
        return;
    }
    ensureFontFaces();
    if (FontRegistry::instance().empty()) {
        // No manifest: the legacy family is the last resort rather than no CJK
        // at all.
        auto cjk = std::make_unique<LoadedFace>();
        cjk->id = std::string("legacy:") + FontRegistry::scriptId(script);
        cjk->script = script;
        cjk->font.load(FontFamily::SourceHanSans);
        if (cjk->font.valid()) {
            faces_.push_back(std::move(cjk));
            ++fontEpoch_;
        } else {
            UNISCOPE_D2_LOG_ERROR("Renderer2D: no CJK face available, CJK text may be missing\n");
        }
        return;
    }

    const std::vector<std::string>* texts = nullptr;
    if (useUiTexts && !uiFallbackTexts_.empty()) {
        texts = &uiFallbackTexts_;
    }
    // An empty request means the default slice: the pre-content stopgap, used
    // when the caller wants "some CJK" and nothing better is on offer.
    std::vector<std::string> textsToBake;
    if (texts != nullptr) {
        textsToBake = *texts;
    }
    std::vector<std::string> baked;
    if (texts != nullptr) {
        baked = bakeFaces(script, FontRole::Body, textsToBake, nullptr);
    }
    if (baked.empty()) {
        bakeFaces(script, FontRole::Body, {}, nullptr);
    }
}

void Renderer2D::setUiFallbackTexts(std::vector<std::string> texts) {
    uiFallbackTexts_ = std::move(texts);
}

bool Renderer2D::preloadScriptFace(FontScript script) {
    if (static_cast<std::size_t>(script) >= static_cast<std::size_t>(FontScript::Count)) {
        return false;
    }
    ensureScriptFont(script, true);
    return firstBakedFace(script, FontRole::Body) != nullptr;
}

void Renderer2D::preloadCjkFont() {
    ensureCjkFont();
}

void Renderer2D::setTextScript(FontScript script) {
    if (static_cast<std::size_t>(script) >= static_cast<std::size_t>(FontScript::Count)) {
        return;
    }
    if (script == textScript_) {
        return;
    }
    textScript_ = script;
    // No atlas changed, but every cached measurement did: the same string now
    // resolves to another face's advance widths.
    ++fontEpoch_;
}

void Renderer2D::setTextRole(FontRole role) {
    if (static_cast<std::size_t>(role) >= static_cast<std::size_t>(FontRole::Count)) {
        return;
    }
    if (role == textRole_) {
        return;
    }
    textRole_ = role;
    ++fontEpoch_;
}

bool Renderer2D::loadFontManifest() {
    // Recorded either way: a second probe of the same missing file is a
    // filesystem walk per frame, and "absent" is a stable answer.
    manifestLoaded_ = true;
    if (!FontRegistry::instance().empty()) {
        return true;
    }
    if (!FontRegistry::instance().load()) {
        // Left empty on purpose: initializeSdfFont() falls back to the legacy
        // two-family path, which is better than no text at all.
        return false;
    }
    return true;
}

bool Renderer2D::bakeFace(
    const FontFace& face,
    const std::vector<std::string>& texts,
    bool contentDriven) {
    std::string path;
    if (!resolveFontPath(face, &path)) {
        UNISCOPE_D2_LOG_ERROR(
            "Renderer2D: font face '%s' is registered but its file is missing (%s)\n",
            face.id.c_str(),
            face.path.c_str());
        return false;
    }

    auto loaded = std::make_unique<LoadedFace>();
    loaded->id = face.id;
    loaded->script = face.script;
    loaded->role = face.role;
    loaded->order = face.order;
    loaded->contentDriven = contentDriven;
    const bool baked = loaded->font.loadFromPath(
        face.id, path, face.script != FontScript::Latin, contentDriven ? &texts : nullptr, nullptr);
    if (!baked) {
        return false;
    }

    for (std::unique_ptr<LoadedFace>& existing : faces_) {
        if (existing->id == loaded->id) {
            existing = std::move(loaded);
            sdfBatchFont_ = nullptr;
            ++fontEpoch_;
            return true;
        }
    }
    faces_.push_back(std::move(loaded));
    sdfBatchFont_ = nullptr;
    ++fontEpoch_;
    return true;
}

bool Renderer2D::bakeScriptFace(
    FontScript script,
    FontRole role,
    const std::vector<std::string>& texts,
    bool contentDriven) {
    const FontFace* face = FontRegistry::instance().primary(script, role);
    if (face == nullptr) {
        return false;
    }
    return bakeFace(*face, texts, contentDriven);
}

std::vector<std::string> Renderer2D::bakeFaces(
    FontScript script,
    FontRole role,
    const std::vector<std::string>& codepointTexts,
    std::vector<SdfFontLoadReport>* reports) {
    std::vector<std::string> baked;
    if (reports != nullptr) {
        reports->clear();
    }
    ensureFontFaces();
    if (!manifestLoaded_ || FontRegistry::instance().empty()) {
        return baked;
    }

    const std::vector<const FontFace*> chain = FontRegistry::instance().chain(script, role);
    if (chain.empty()) {
        UNISCOPE_D2_LOG_ERROR(
            "Renderer2D: no font face registered for script '%s' role '%s'\n",
            FontRegistry::scriptId(script),
            FontRegistry::roleId(role));
        return baked;
    }

    // The characters still unaccounted for. Every fallback face is handed just
    // these, so it costs the gap it fills instead of the whole alphabet again.
    //
    // An empty request means "the default slice" - the pre-content stopgap a
    // lazy load uses - and that is the primary face's job alone.
    const bool contentDriven = !codepointTexts.empty();
    std::vector<std::uint32_t> remaining = collectCodepoints(codepointTexts);

    for (const FontFace* face : chain) {
        // Already baked and already holding every character it would be asked
        // for: the bake would rebuild the atlas that is in memory. The loading
        // screen measures labels before the content bake, so the Latin face
        // exists from that first, smaller bake.
        if (const LoadedFace* existing = loadedFace(face->id); existing != nullptr && contentDriven) {
            bool covered = true;
            for (std::uint32_t codepoint : remaining) {
                if (!existing->font.hasGlyph(codepoint)) {
                    covered = false;
                    break;
                }
            }
            if (covered) {
                baked.push_back(face->id);
                remaining.clear();
                if (reports != nullptr) {
                    reports->push_back(SdfFontLoadReport{});
                }
                break;
            }
        }

        std::vector<std::string> texts;
        texts.reserve(remaining.size());
        for (std::uint32_t codepoint : remaining) {
            texts.push_back(encodeUtf8(codepoint));
        }

        if (!bakeFace(*face, texts, contentDriven)) {
            if (reports != nullptr) {
                reports->push_back(SdfFontLoadReport{});
            }
            continue;
        }
        baked.push_back(face->id);

        SdfFontLoadReport report;
        report.requested = static_cast<int>(remaining.size());
        for (std::uint32_t codepoint : remaining) {
            const LoadedFace* loaded = nullptr;
            for (const std::unique_ptr<LoadedFace>& candidate : faces_) {
                if (candidate->id == face->id) {
                    loaded = candidate.get();
                    break;
                }
            }
            if (loaded == nullptr) {
                break;
            }
            if (loaded->font.hasGlyph(codepoint)) {
                ++report.baked;
            } else {
                report.missing.push_back(codepoint);
            }
        }
        if (reports != nullptr) {
            reports->push_back(std::move(report));
        }

        // Whatever this face can draw is no longer a fallback concern.
        remaining.erase(
            std::remove_if(
                remaining.begin(),
                remaining.end(),
                [&](std::uint32_t codepoint) {
                    const uint32_t probe = codepoint;
                    for (const std::unique_ptr<LoadedFace>& candidate : faces_) {
                        if (candidate->id == face->id) {
                            return candidate->font.hasGlyph(probe);
                        }
                    }
                    return false;
                }),
            remaining.end());

        if (remaining.empty() || !contentDriven) {
            break;
        }
    }
    return baked;
}

bool Renderer2D::bakeAllScripts(
    const std::vector<std::string>& latinTexts,
    const std::vector<std::string>& chineseTexts,
    const std::vector<std::string>& japaneseTexts) {
    if (!hasDeviceResources()) {
        return false;
    }
    bool any = false;
    any = !bakeFaces(FontScript::Latin, FontRole::Body, latinTexts, nullptr).empty() || any;
    any = !bakeFaces(FontScript::Chinese, FontRole::Body, chineseTexts, nullptr).empty() || any;
    any = !bakeFaces(FontScript::Japanese, FontRole::Body, japaneseTexts, nullptr).empty() || any;

    // Faces the manifest marked eager are baked even though no chain asked for
    // them: the author spends the atlas and the bake time up front so that a
    // role (a display weight, an italic) is ready the moment a screen asks.
    for (const FontFace& face : FontRegistry::instance().faces()) {
        if (!face.eager || loadedFace(face.id) != nullptr) {
            continue;
        }
        const std::vector<std::string>& texts = face.script == FontScript::Chinese
            ? chineseTexts
            : (face.script == FontScript::Japanese ? japaneseTexts : latinTexts);
        any = bakeFace(face, texts, !texts.empty()) || any;
    }
    return any;
}

bool Renderer2D::bakeCjkFont(
    const std::vector<std::string>& codepointTexts,
    SdfFontLoadReport* report) {
    if (!hasDeviceResources()) {
        return false;
    }
    const FontScript script = textScript_ == FontScript::Latin ? FontScript::Chinese : textScript_;
    std::vector<SdfFontLoadReport> reports;
    const std::vector<std::string> baked =
        bakeFaces(script, FontRole::Body, codepointTexts, &reports);
    if (report != nullptr) {
        // The union the caller cares about: what is still blank after the
        // whole chain had its turn.
        report->missing = missingGlyphs(script, codepointTexts);
        int bakedGlyphs = 0;
        for (const SdfFontLoadReport& perFace : reports) {
            bakedGlyphs += perFace.baked;
        }
        report->baked = bakedGlyphs;
        report->requested = bakedGlyphs + static_cast<int>(report->missing.size());
    }
    return !baked.empty();
}

const Renderer2D::LoadedFace* Renderer2D::loadedFace(const std::string& id) const {
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->id == id) {
            return face.get();
        }
    }
    return nullptr;
}

const SdfFont* Renderer2D::firstBakedFace(FontScript script, FontRole role) const {
    const SdfFont* best = nullptr;
    int bestOrder = 0;
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (!face->font.valid() || face->script != script) {
            continue;
        }
        if (face->role != role) {
            continue;
        }
        if (best == nullptr || face->order < bestOrder) {
            best = &face->font;
            bestOrder = face->order;
        }
    }
    return best;
}

const SdfFont* Renderer2D::metricsFont() const {
    if (const SdfFont* latin = firstBakedFace(FontScript::Latin, FontRole::Body)) {
        return latin;
    }
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->font.valid()) {
            return &face->font;
        }
    }
    return nullptr;
}

bool Renderer2D::faceIsContentDriven(FontScript script) const {
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->script == script && face->contentDriven) {
            return true;
        }
    }
    return false;
}

std::size_t Renderer2D::bakedFaceCount() const {
    return faces_.size();
}

std::vector<std::string> Renderer2D::bakedFaceIds() const {
    std::vector<std::string> ids;
    ids.reserve(faces_.size());
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->font.valid()) {
            ids.push_back(face->id);
        }
    }
    return ids;
}

std::vector<std::uint32_t> Renderer2D::missingGlyphs(
    FontScript script,
    const std::vector<std::string>& texts) const {
    std::vector<std::uint32_t> missing;
    for (std::uint32_t codepoint : collectCodepoints(texts)) {
        bool found = false;
        for (const std::unique_ptr<LoadedFace>& face : faces_) {
            if (face->script == script && face->font.valid() && face->font.hasGlyph(codepoint)) {
                found = true;
                break;
            }
        }
        if (!found) {
            missing.push_back(codepoint);
        }
    }
    return missing;
}

bool Renderer2D::hasCjkGlyph(std::uint32_t codepoint) const {
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->script != FontScript::Latin && face->font.valid() && face->font.hasGlyph(codepoint)) {
            return true;
        }
    }
    return false;
}

bool Renderer2D::cjkFontCovers(const std::vector<std::string>& codepointTexts) const {
    // The default slice is a guess about the content; the content knows its
    // own characters. An atlas that is not content-driven never counts as
    // covering them.
    const bool contentDriven =
        faceIsContentDriven(FontScript::Chinese) || faceIsContentDriven(FontScript::Japanese);
    if (!contentDriven) {
        return false;
    }
    for (std::uint32_t codepoint : collectCodepoints(codepointTexts)) {
        if (!hasCjkGlyph(codepoint)) {
            return false;
        }
    }
    return true;
}

bool Renderer2D::hasAnySdfFont() const {
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->font.valid()) {
            return true;
        }
    }
    return false;
}

const SdfFont& Renderer2D::fontForCodepoint(std::uint32_t codepoint) const {
    if (faces_.empty()) {
        const_cast<Renderer2D*>(this)->initializeSdfFont();
    }

    // 1. The active script's chain, at the active role and then at body. A face
    //    with an incomplete set of the script's characters simply does not win
    //    a codepoint it lacks: the next face in the chain does, on the same line.
    auto firstWithGlyph = [&](FontScript script, FontRole role) -> const SdfFont* {
        const SdfFont* best = nullptr;
        int bestOrder = 0;
        for (const std::unique_ptr<LoadedFace>& face : faces_) {
            if (!face->font.valid() || face->script != script || face->role != role) {
                continue;
            }
            if (!face->font.hasGlyph(codepoint)) {
                continue;
            }
            if (best == nullptr || face->order < bestOrder) {
                best = &face->font;
                bestOrder = face->order;
            }
        }
        return best;
    };

    if (textScript_ != FontScript::Latin) {
        if (const SdfFont* face = firstWithGlyph(textScript_, textRole_)) {
            return *face;
        }
        if (textRole_ != FontRole::Body) {
            if (const SdfFont* face = firstWithGlyph(textScript_, FontRole::Body)) {
                return *face;
            }
        }
        // The other CJK script can still hold a character this one lacks (a
        // rare kanji, a symbol the Chinese face never drew).
        const FontScript alternate =
            textScript_ == FontScript::Chinese ? FontScript::Japanese : FontScript::Chinese;
        if (const SdfFont* face = firstWithGlyph(alternate, FontRole::Body)) {
            return *face;
        }
    }

    // 2. Latin. An English label in a Chinese session is drawn by the Chinese
    //    face, because step 1 already claimed it; this is the fallback for
    //    scripts whose face lacks the glyph.
    if (const SdfFont* face = firstWithGlyph(FontScript::Latin, textRole_)) {
        return *face;
    }
    if (textRole_ != FontRole::Body) {
        if (const SdfFont* face = firstWithGlyph(FontScript::Latin, FontRole::Body)) {
            return *face;
        }
    }

    // 3. Any baked face that happens to have the glyph (a spare weight baked
    //    for a special screen, say), then the metric base.
    for (const std::unique_ptr<LoadedFace>& face : faces_) {
        if (face->font.valid() && face->font.hasGlyph(codepoint)) {
            return face->font;
        }
    }

    // 4. Nothing baked holds it. Before any content load the only faces are the
    //    Latin ones, and the options menu can still print 简体中文 / 日本語 -
    //    so ask for the script's UI slice on demand (cheap: the UI strings, not
    //    the thousand-glyph default). A Japanese session also needs the Chinese
    //    face for a simplified name, hence the second pass.
    if (codepoint >= 0x0300) {
        Renderer2D* self = const_cast<Renderer2D*>(this);
        const FontScript active =
            textScript_ == FontScript::Latin ? FontScript::Chinese : textScript_;
        self->ensureScriptFont(active, true);
        for (const std::unique_ptr<LoadedFace>& face : faces_) {
            if (face->font.valid() && face->font.hasGlyph(codepoint)) {
                return face->font;
            }
        }
        const FontScript alternate =
            active == FontScript::Chinese ? FontScript::Japanese : FontScript::Chinese;
        self->ensureScriptFont(alternate, true);
        for (const std::unique_ptr<LoadedFace>& face : faces_) {
            if (face->font.valid() && face->font.hasGlyph(codepoint)) {
                return face->font;
            }
        }
    }

    if (const SdfFont* metrics = metricsFont()) {
        return *metrics;
    }
    return emptyFace();
}

bool Renderer2D::hasDeviceResources() const {
    return shaders_.has(ShaderId::Rect2D)
        && shaders_.has(ShaderId::Textured2D)
        && squareVao_ != nullptr
        && squareVbo_ != nullptr
        && texturedVao_ != nullptr
        && texturedVbo_ != nullptr;
}

bool Renderer2D::hasSpriteBatch() const {
    return hasDeviceResources()
        && shaders_.has(ShaderId::Textured2DBatch)
        && batchVao_ != nullptr
        && batchVbo_ != nullptr;
}

bool Renderer2D::hasRectBatch() const {
    return hasDeviceResources()
        && shaders_.has(ShaderId::Rect2DBatch)
        && batchVao_ != nullptr
        && batchVbo_ != nullptr;
}

bool Renderer2D::hasSdfTextBatch() const {
    return hasDeviceResources()
        && hasAnySdfFont()
        && shaders_.has(ShaderId::SdfTextBatch)
        && batchVao_ != nullptr
        && batchVbo_ != nullptr;
}

void Renderer2D::setEffectQuality(float quality01) {
    effectQuality_ = std::clamp(quality01, 0.0f, 1.0f);
}

void Renderer2D::setQualityPreset(TextureQuality quality) {
    qualityTier_ = quality;
    const QualityPreset2D& preset = qualityPreset(quality);
    effectQuality_ = preset.effectQuality;
}

void Renderer2D::setSceneMultisample(int samples) {
    sceneSamples_ = std::max(0, samples);
}

void Renderer2D::setShaderTime(float seconds) {
    shaderTimeOverride_ = std::max(0.0f, seconds);
}

void Renderer2D::setEdgeStyleTable(const EdgeStyleTable& table) {
    edgeStyleTable_ = table;
    edgeStyleTableDirty_ = true;
}

void Renderer2D::setMaterialLightDirection(float x, float y) {
    const float length = std::sqrt(x * x + y * y);
    if (length <= 0.0001f) {
        materialLightDirectionX_ = -0.42f;
        materialLightDirectionY_ = 0.68f;
        return;
    }
    materialLightDirectionX_ = x / length;
    materialLightDirectionY_ = y / length;
}

void Renderer2D::setAmbientLight(float red, float green, float blue, float intensity) {
    ambientR_ = red;
    ambientG_ = green;
    ambientB_ = blue;
    ambientIntensity_ = intensity;
}

void Renderer2D::setDaylightFactor(float daylight01) {
    daylightFactor_ = std::clamp(daylight01, 0.0f, 1.0f);
}

void Renderer2D::registerSpriteMaterialMask(const Texture2D* albedo, const Texture2D* mask) {
    if (albedo == nullptr || !albedo->valid() || mask == nullptr || !mask->valid()) {
        return;
    }
    spriteMaterialMasks_[albedo->id()] = mask;
}

void Renderer2D::setShadowEnabled(bool enabled) {
    shadowEnabled_ = enabled;
}

void Renderer2D::setShadowColor(float red, float green, float blue, float alpha) {
    shadowR_ = red;
    shadowG_ = green;
    shadowB_ = blue;
    shadowA_ = std::clamp(alpha, 0.0f, 1.0f);
}

void Renderer2D::setShadowLength(float length) {
    shadowLength_ = std::max(0.0f, length);
}

// Lateral shadow distortion: oblique sun spreads the silhouette wide,
// steep sun keeps it tight. 1.0 = no distortion.
void Renderer2D::setShadowWidthScale(float scale) {
    shadowWidthScale_ = std::clamp(scale, 0.5f, 2.0f);
}

void Renderer2D::setShadowCasters(const ShadowCaster2D* casters) {
    shadowCasters_ = casters;
}

void Renderer2D::setContactShadowCasters(const ShadowCaster2D* casters) {
    contactShadowCasters_ = casters;
}

void Renderer2D::setContactShadowAlpha(float alpha) {
    contactShadowAlpha_ = std::clamp(alpha, 0.0f, 1.0f);
}

void Renderer2D::setAmbientOcclusionStrength(float strength01) {
    aoStrength_ = std::clamp(strength01, 0.0f, 1.0f);
}

void Renderer2D::setSubsurfaceStrength(float strength01) {
    sssStrength_ = std::clamp(strength01, 0.0f, 1.0f);
}

void Renderer2D::setSubsurfaceColor(float red, float green, float blue) {
    sssColorR_ = red;
    sssColorG_ = green;
    sssColorB_ = blue;
}

void Renderer2D::setFog(float red, float green, float blue, float density) {
    fogR_ = red;
    fogG_ = green;
    fogB_ = blue;
    fogDensity_ = std::max(density, 0.0f);
}

void Renderer2D::setFogCenter(float x, float y) {
    fogCenterX_ = x;
    fogCenterY_ = y;
}

void Renderer2D::setFogWorldScale(float viewportPerWorldX, float viewportPerWorldY) {
    fogWorldScaleX_ = std::max(viewportPerWorldX, 0.0001f);
    fogWorldScaleY_ = std::max(viewportPerWorldY, 0.0001f);
}

void Renderer2D::setVignetteStrength(float strength01) {
    vignetteStrength_ = std::clamp(strength01, 0.0f, 1.0f);
}

void Renderer2D::setSpriteAdditive(bool additive) {
    if (spriteAdditive_ == additive) {
        return;
    }
    flushSpriteBatch();
    flushRectBatch();
    spriteAdditive_ = additive;
}

void Renderer2D::setDynamicResolution(bool autoMode, float fixedScale, float minScale) {
    dynamicAuto_ = autoMode;
    dynamicFixedScale_ = std::clamp(fixedScale, 0.25f, 1.0f);
    dynamicMinScale_ = std::clamp(minScale, 0.25f, 1.0f);
    if (!dynamicAuto_) {
        dynamicScale_ = dynamicFixedScale_;
    }
}

void Renderer2D::updateDynamicResolution(float deltaSeconds) {
    // EMA of the presented frame time. The auto controller eases the render
    // scale down when the average misses 45fps and back up past 58fps, with a
    // short cooldown so it does not hunt on alternating frames.
    const float dt = std::clamp(deltaSeconds, 0.0001f, 0.25f);
    dynamicFrameTimeEma_ += (dt - dynamicFrameTimeEma_) * 0.06f;
    if (!dynamicAuto_) {
        dynamicScale_ = dynamicFixedScale_;
    } else if (dynamicCooldown_ > 0.0f) {
        dynamicCooldown_ -= dt;
    } else if (dynamicFrameTimeEma_ > 1.0f / 45.0f && dynamicScale_ > dynamicMinScale_) {
        dynamicScale_ = std::max(dynamicMinScale_, dynamicScale_ - 0.05f);
        dynamicCooldown_ = 0.25f;
    } else if (dynamicFrameTimeEma_ < 1.0f / 58.0f && dynamicScale_ < 1.0f) {
        dynamicScale_ = std::min(1.0f, dynamicScale_ + 0.025f);
        dynamicCooldown_ = 0.5f;
    }

    // Quality-tier sliding: the tier is the coarser lever (it sheds shader
    // effects, shadow resolution and sampler cost in one step, and a switch
    // rebuilds the shadow buffers), so it moves on a wider band and a much
    // longer cooldown than the render scale. Promotion only happens once the
    // cheaper lever is already maxed out; demotion drops the render scale to
    // the top as well, so a recovered frame doesn't promote straight back.
    if (qualityAuto_) {
        if (qualityCooldown_ > 0.0f) {
            qualityCooldown_ -= dt;
        } else {
            const int tier = static_cast<int>(qualityTier_);
            if (dynamicFrameTimeEma_ > 1.0f / 40.0f
                && tier > static_cast<int>(TextureQuality::Low)) {
                setQualityPreset(static_cast<TextureQuality>(tier - 1));
                dynamicScale_ = 1.0f;
                dynamicCooldown_ = 1.0f;
                qualityCooldown_ = 2.0f;
            } else if (dynamicFrameTimeEma_ < 1.0f / 59.0f
                && tier < static_cast<int>(TextureQuality::Ultra)
                && dynamicScale_ >= 0.999f) {
                setQualityPreset(static_cast<TextureQuality>(tier + 1));
                dynamicCooldown_ = 1.0f;
                qualityCooldown_ = 4.0f;
            }
        }
    }
}

bool Renderer2D::beginDynamicScene(int framebufferWidth, int framebufferHeight) {
    dynamicSceneActive_ = false;
    if (!hasDeviceResources() || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }
    const float scale = dynamicAuto_ ? dynamicScale_ : dynamicFixedScale_;
    dynamicScale_ = std::clamp(scale, dynamicMinScale_, 1.0f);
    // An offscreen pass is needed either because the world renders below native
    // resolution, or because MSAA samples the scene (the default framebuffer
    // carries its own samples, but this renderer's own target has to ask for
    // them). At native resolution with no MSAA the world draws straight to the
    // screen and no upscale pass runs.
    const bool needsOffscreen = dynamicScale_ < 0.999f || sceneSamples_ > 1;
    if (!needsOffscreen) {
        return false;
    }
    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();
    const int targetW = std::max(2, static_cast<int>(framebufferWidth * dynamicScale_));
    const int targetH = std::max(2, static_cast<int>(framebufferHeight * dynamicScale_));
    if (!dynamicTarget_.valid()
        || dynamicTarget_.width() != targetW
        || dynamicTarget_.height() != targetH
        || dynamicTarget_.samples() != sceneSamples_) {
        if (!dynamicTarget_.create(targetW, targetH, RenderTargetFormat::Rgba8, sceneSamples_)) {
            return false;
        }
    }
    dynamicScreenWidth_ = framebufferWidth;
    dynamicScreenHeight_ = framebufferHeight;
    dynamicTarget_.bind();
    noteRenderTargetChanged();
    glClearColor(0.022f, 0.018f, 0.012f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    dynamicSceneActive_ = true;
    return true;
}

void Renderer2D::endDynamicSceneAndPresent(float sharpness) {
    if (!dynamicSceneActive_ || !dynamicTarget_.valid()) {
        dynamicSceneActive_ = false;
        return;
    }
    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();
    // MSAA: copy the multisampled draw surface into the resolve texture the
    // upscale shader samples. Single-sampled targets make this a no-op.
    dynamicTarget_.resolve();
    dynamicTarget_.unbind(dynamicScreenWidth_, dynamicScreenHeight_);
    noteRenderTargetChanged();
    dynamicSceneActive_ = false;
    if (!hasDeviceResources()) {
        return;
    }
    ShaderProgram* shader = nullptr;
    try {
        shader = &shaders_.program(ShaderId::Upscale);
    } catch (const std::exception& error) {
        logBatchInitFailure("upscale shader", error);
        return;
    }
    // Fullscreen textured quad in NDC; the scene texture's own texel (0,0) is
    // the bottom-left of the rendered image, so identity UVs need no flip.
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,

        -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
    };

    glDisable(GL_BLEND);
    glBindVertexArray(texturedVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, texturedVbo_->id());
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    shader->use();
    shader->setInt("u_scene", 0);
    shader->setVec2(
        "u_texel",
        1.0f / std::max(static_cast<float>(dynamicTarget_.width()), 1.0f),
        1.0f / std::max(static_cast<float>(dynamicTarget_.height()), 1.0f));
    shader->setFloat("u_sharpness", std::clamp(sharpness, 0.0f, 1.0f));
    shader->setInt("u_bicubic", qualityPreset(qualityTier_).bicubicUpscale ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, dynamicTarget_.colorTexture().id);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
}

void Renderer2D::setSpriteNormalTexture(const Texture2D* normalTexture) {
    spriteNormalTexture_ = (normalTexture != nullptr && normalTexture->valid())
        ? normalTexture
        : nullptr;
}

void Renderer2D::setSpriteDetailTexture(const Texture2DArray* detailTexture) {
    spriteDetailTexture_ = (detailTexture != nullptr && detailTexture->valid())
        ? detailTexture
        : nullptr;
}

void Renderer2D::setBuildingTextureArray(const Texture2DArray* textureArray) {
    buildingTextureArray_ = (textureArray != nullptr && textureArray->valid())
        ? textureArray
        : nullptr;
}

void Renderer2D::setBuildingArrayLayer(int layer) {
    currentBuildingArrayLayer_ = layer;
}

bool Renderer2D::enqueueBuildingArraySprite(
    float x, float y, float halfWidth, float halfHeight,
    UvRect uv, float red, float green, float blue, float alpha,
    float blendRed, float blendGreen, float blendBlue,
    float blendLeft, float blendRight, float blendDown, float blendUp,
    MaterialParams2D material, UvRect nextFrameUv, float frameBlend,
    int uvOrientation) {
    if (!batchActive_ || !hasSpriteBatch() || halfWidth <= 0.0f || halfHeight <= 0.0f) {
        return false;
    }
    if (!buildingTextureArray_ || !buildingTextureArray_->valid()) {
        return false;
    }
    if (currentBuildingArrayLayer_ < 0) {
        return false;
    }

    const unsigned int textureId = buildingTextureArray_->id();
    if (batchTextureId_ != 0 && batchTextureId_ != textureId) {
        flushSpriteBatch();
        batchActive_ = true;
    }

    const bool castsShadow = shadowEnabled_
        && (static_cast<int>(material.flags) & static_cast<int>(MaterialFlags2D::CastShadow)) != 0
        && !material.noTextureShadow
        && shadowLength_ > 0.0001f
        && shadowA_ > 0.004f;
    const std::size_t neededVertices = castsShadow ? 12 : 6;
    if (spriteBatch_.size() + neededVertices > kMaxBatchVertices) {
        flushSpriteBatch();
        batchActive_ = true;
    }

    batchTextureId_ = textureId;

    if (castsShadow) {
        const float safeAspect = std::max(batchAspect_, 0.0001f);
        const float shadowShearX = -materialLightDirectionX_ * halfHeight * shadowLength_ / safeAspect;
        const float shadowShearY = -materialLightDirectionY_ * halfHeight * shadowLength_;
        MaterialParams2D shadowMaterial = material;
        shadowMaterial.kind = MaterialKind2D::Shadow;
        shadowMaterial.flags = MaterialFlags2D::FlatSurface;
        shadowMaterial.wetness = 0.0f;
        shadowMaterial.emissive = 0.0f;
        shadowMaterial.detail = {};
        shadowMaterial.cornerStyle = 0;
        shadowMaterial.edgeMask = 0;
        appendSpriteQuad(
            x, y, halfWidth * shadowWidthScale_, halfHeight, uv,
            shadowR_, shadowG_, shadowB_, shadowA_,
            1.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            shadowMaterial, nextFrameUv, frameBlend,
            1.0f, 0.0f, 0.0f, 0.0f,
            shadowShearX, shadowShearY, uvOrientation);
    }

    appendSpriteQuad(
        x, y, halfWidth, halfHeight, uv,
        red, green, blue, alpha,
        blendRed, blendGreen, blendBlue,
        blendLeft, blendRight, blendDown, blendUp,
        material, nextFrameUv, frameBlend,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, uvOrientation);
    return true;
}

void Renderer2D::setDetailOrigin(float x, float y) {
    detailOriginX_ = x;
    detailOriginY_ = y;
}

void Renderer2D::drawMenuBackground(
    float framebufferAspect,
    float backgroundR,
    float backgroundG,
    float backgroundB,
    float accentR,
    float accentG,
    float accentB,
    float panelR,
    float panelG,
    float panelB) {
    if (!hasDeviceResources()) {
        return;
    }

    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();

    ShaderProgram* shader = nullptr;
    try {
        shader = &shaders_.program(ShaderId::MenuBackground);
    } catch (const std::exception& error) {
        logBatchInitFailure("menu background shader", error);
        drawRect(0.0f, 0.0f, framebufferAspect, 1.0f, framebufferAspect, backgroundR, backgroundG, backgroundB);
        return;
    }

    glDisable(GL_BLEND);
    shader->use();
    shader->setVec3("u_background", backgroundR, backgroundG, backgroundB);
    shader->setVec3("u_accent", accentR, accentG, accentB);
    shader->setVec3("u_panel", panelR, panelG, panelB);
    shader->setFloat("u_time", shaderTimeSeconds_);
    shader->setFloat("u_aspect", framebufferAspect);
    shader->setFloat("u_effect_quality", effectQuality_);

    glBindVertexArray(squareVao_->id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer2D::drawCinematicBackdrop(
    float framebufferAspect,
    float deepR,
    float deepG,
    float deepB,
    float hazeR,
    float hazeG,
    float hazeB,
    float glowR,
    float glowG,
    float glowB) {
    if (!hasDeviceResources()) {
        return;
    }

    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();

    ShaderProgram* shader = nullptr;
    try {
        shader = &shaders_.program(ShaderId::CinematicBackdrop);
    } catch (const std::exception& error) {
        logBatchInitFailure("cinematic backdrop shader", error);
        // Without the shader the field is a flat haze: still a backdrop, just
        // a duller one. The words stay readable over it.
        drawRect(0.0f, 0.0f, framebufferAspect, 1.0f, framebufferAspect, deepR, deepG, deepB);
        return;
    }

    glDisable(GL_BLEND);
    shader->use();
    shader->setVec3("u_deep", deepR, deepG, deepB);
    shader->setVec3("u_haze", hazeR, hazeG, hazeB);
    shader->setVec3("u_glow", glowR, glowG, glowB);
    shader->setFloat("u_time", shaderTimeSeconds_);
    shader->setFloat("u_aspect", framebufferAspect);
    shader->setFloat("u_effect_quality", effectQuality_);

    glBindVertexArray(squareVao_->id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer2D::drawWhiteOut(float framebufferAspect, float radius, float intensity) {
    if (!hasDeviceResources()) {
        return;
    }

    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();

    ShaderProgram* shader = nullptr;
    try {
        shader = &shaders_.program(ShaderId::WhiteOut);
    } catch (const std::exception& error) {
        logBatchInitFailure("whiteout shader", error);
        // Without the shader the light cannot bloom, so the scene cuts in
        // plain. That is still a cut; it is just not a sunrise.
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    shader->use();
    shader->setFloat("u_radius", radius);
    shader->setFloat("u_intensity", intensity);
    shader->setFloat("u_aspect", framebufferAspect);

    glBindVertexArray(squareVao_->id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer2D::beginFrame() {
    if (!hasDeviceResources()) {
        return;
    }

    rectBatchFlushes_ = 0;
    sdfBatchFlushes_ = 0;
    spriteBatchFlushes_ = 0;
    // A new frame may draw into a different destination than the last one.
    noteRenderTargetChanged();
    // Drop the triangulation cache key (not the vertex storage): the next
    // frame's caster set may live at the same address with new content.
    // Also drop the caster pointer itself: caster sets are per-frame stack
    // objects, and a new frame must never sample the previous one.
    triangulatedCasters_ = nullptr;
    triangulatedContactCasters_ = nullptr;
    shadowCasters_ = nullptr;
    contactShadowCasters_ = nullptr;
    contactShadowAlpha_ = 0.0f;
    // Shadow cache is implicitly invalidated by the frame index increment,
    // but we also clear the caster pointer for safety.
    cachedShadowCasters_ = nullptr;
    // Shader time: allow a one-frame override from setShaderTime().
    if (shaderTimeOverride_ >= 0.0f) {
        shaderTimeSeconds_ = shaderTimeOverride_;
        shaderTimeOverride_ = -1.0f;
    } else {
        ++frameIndex_;
        shaderTimeSeconds_ = static_cast<float>(frameIndex_) * (1.0f / 60.0f);
    }
    glClearColor(0.022f, 0.018f, 0.012f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer2D::beginRectBatch(float framebufferAspect) {
    flushSpriteBatch();
    flushSdfTextBatch();
    rectBatchAspect_ = framebufferAspect;
    rectBatch_.clear();
    rectBatchActive_ = hasRectBatch();
}

void Renderer2D::beginTextBatch(float framebufferAspect) {
    flushSdfTextBatch();
    sdfFlushSuspended_ = true;
    sdfBatchAspect_ = framebufferAspect;
}

void Renderer2D::endTextBatch() {
    sdfFlushSuspended_ = false;
    flushSdfTextBatch();
}

void Renderer2D::appendClipRectQuad(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float red,
    float green,
    float blue,
    float alpha) {
    const float safeAspect = std::max(rectBatchAspect_, 0.0001f);
    const auto clipCorner = [&](float localX, float localY) -> std::pair<float, float> {
        const float px = localX * halfWidth + x;
        const float py = localY * halfHeight + y;
        return {px / safeAspect, py};
    };

    const auto [x0, y0] = clipCorner(-1.0f, -1.0f);
    const auto [x1, y1] = clipCorner(1.0f, -1.0f);
    const auto [x2, y2] = clipCorner(1.0f, 1.0f);
    const auto [x3, y3] = clipCorner(-1.0f, 1.0f);

    const BatchVertex tri0[] = {
        {x0, y0, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {x1, y1, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {x2, y2, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {x0, y0, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {x2, y2, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {x3, y3, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
    };

    rectBatch_.insert(rectBatch_.end(), std::begin(tri0), std::end(tri0));
}

void Renderer2D::appendClipViewportQuad(
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
    float alpha) {
    const float safeAspect = std::max(rectBatchAspect_, 0.0001f);
    const auto toClip = [&](float px, float py) -> std::pair<float, float> {
        return {px / safeAspect, py};
    };

    const auto [cx0, cy0] = toClip(x0, y0);
    const auto [cx1, cy1] = toClip(x1, y1);
    const auto [cx2, cy2] = toClip(x2, y2);
    const auto [cx3, cy3] = toClip(x3, y3);

    const BatchVertex tri0[] = {
        {cx0, cy0, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {cx1, cy1, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {cx2, cy2, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {cx0, cy0, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {cx2, cy2, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
        {cx3, cy3, 0.0f, 0.0f, 0.0f, red, green, blue, alpha},
    };

    rectBatch_.insert(rectBatch_.end(), std::begin(tri0), std::end(tri0));
}

bool Renderer2D::enqueueClipQuadInternal(
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
    float alpha) {
    if (!rectBatchActive_ || !hasRectBatch() || alpha <= 0.0f) {
        return false;
    }

    if (rectBatchAspect_ != framebufferAspect) {
        flushRectBatch();
        beginRectBatch(framebufferAspect);
    }

    if (rectBatch_.size() + 6 > kMaxBatchVertices) {
        flushRectBatch();
        beginRectBatch(framebufferAspect);
    }

    appendClipViewportQuad(x0, y0, x1, y1, x2, y2, x3, y3, red, green, blue, alpha);
    return true;
}

bool Renderer2D::enqueueRectQuad(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float framebufferAspect,
    float red,
    float green,
    float blue,
    float alpha) {
    if (!rectBatchActive_ || !hasRectBatch() || halfWidth <= 0.0f || halfHeight <= 0.0f || alpha <= 0.0f) {
        return false;
    }

    if (rectBatchAspect_ != framebufferAspect) {
        flushRectBatch();
        beginRectBatch(framebufferAspect);
    }

    if (rectBatch_.size() + 6 > kMaxBatchVertices) {
        flushRectBatch();
        beginRectBatch(framebufferAspect);
    }

    appendClipRectQuad(x, y, halfWidth, halfHeight, red, green, blue, alpha);
    return true;
}

void Renderer2D::flushRectBatch() {
    if (!rectBatchActive_ || rectBatch_.empty() || !hasRectBatch()) {
        rectBatch_.clear();
        rectBatchActive_ = false;
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, spriteAdditive_ ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(batchVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, batchVbo_->id());
    ensureBatchBuffer(rectBatch_.size());
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(rectBatch_.size() * sizeof(BatchVertex)),
        rectBatch_.data());

    ShaderProgram& shader = shaders_.program(ShaderId::Rect2DBatch);
    shader.use();
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rectBatch_.size()));
    ++rectBatchFlushes_;
    glBindVertexArray(0);

    rectBatch_.clear();
    rectBatchActive_ = false;
}

void Renderer2D::beginSpriteBatch(float framebufferAspect) {
    flushRectBatch();
    flushSdfTextBatch();
    batchAspect_ = framebufferAspect;
    batchTextureId_ = 0;
    spriteBatch_.clear();
    batchHasShadowCopies_ = false;
    batchActive_ = hasSpriteBatch();
}

void Renderer2D::appendSpriteQuad(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    UvRect uv,
    float red,
    float green,
    float blue,
    float alpha,
    float blendRed,
    float blendGreen,
    float blendBlue,
    float blendLeft,
    float blendRight,
    float blendDown,
    float blendUp,
    MaterialParams2D material,
    UvRect nextFrameUv,
    float frameBlend,
    float cosAngle,
    float sinAngle,
    float pivotX,
    float pivotY,
    float shearX,
    float shearY,
    int uvOrientation,
    float taperNear,
    float taperFar,
    float mirrorX) {
    const float safeAspect = std::max(batchAspect_, 0.0001f);
    // `pivot` is expressed in the sprite's own -1..1 frame, so a rotated quad
    // can be anchored anywhere between its start and end (a bone's joint, a
    // foot's ankle) without the caller having to recompute the center.
    //
    // taperNear/taperFar scale the half-height of the -x and +x ends. They are
    // 1 by default, which makes the two ends equal and the quad the rectangle
    // it has always been; a tapered limb passes values below 1 to narrow its
    // far end without changing its pivot.
    const auto clipCorner = [&](float localX, float localY) -> std::pair<float, float> {
        const float hh = halfHeight * (localX < 0.0f ? taperNear : taperFar);
        const float ox = (localX + pivotX) * halfWidth;
        const float oy = (localY + pivotY) * hh;
        // mirrorX reflects the quad about its own vertical axis. It multiplies rx
        // only - the reflection is x -> -x, so ry keeps its sign - and is applied
        // before the shear, which is a screen-space offset from the light and must
        // not follow the art.
        const float rx = (ox * cosAngle - oy * sinAngle) * mirrorX;
        const float ry = ox * sinAngle + oy * cosAngle;
        // Shear only the upper half of the quad to fake a ground projection:
        // the bottom stays anchored at the caster while the top stretches away
        // from the light. T ranges from 0 at the bottom to 1 at the top.
        const float t = (localY + 1.0f) * 0.5f;
        const float px = rx + x + shearX * t;
        const float py = ry + y + shearY * t;
        return {px / safeAspect, py};
    };

    const auto [x0, y0] = clipCorner(-1.0f, -1.0f);
    const auto [x1, y1] = clipCorner(1.0f, -1.0f);
    const auto [x2, y2] = clipCorner(1.0f, 1.0f);
    const auto [x3, y3] = clipCorner(-1.0f, 1.0f);

    const float kind = static_cast<float>(static_cast<int>(material.kind));
    const float wetness = material.wetness;
    const float emissive = material.emissive;
    // Which slice of the detail array this material reads. It rides in the slot
    // that used to carry a detail-map rotation nothing ever set, so selecting a
    // per-material detail field cost no extra vertex bandwidth. As a float it is
    // exact for any layer count a real texture has.
    const float detailLayer = static_cast<float>(material.detail.layer);
    const float detailScale = material.detail.scale;
    const float detailStrength = material.detail.strength;
    const float detailOffsetU = material.detail.offsetU;
    const float detailOffsetV = material.detail.offsetV;
    const float occluded = material.occluded;
    // One float carries flags, a five-bit tile-stable corner seed and the
    // per-side/per-corner edge description. The complete 24-bit value remains
    // exactly representable by a float, without another vertex attribute.
    //
    // Layout: bits 0-2 = MaterialFlags2D (FlatSurface, NoRim, CastShadow),
    // bits 3-7 = cornerStyle, bits 8-23 = edgeMask. cornerStyle was reduced
    // from six to five bits to make room for CastShadow; 32 radius variants
    // per tile is still plenty for the corner silhouette randomisation.
    const int packedFlags = (static_cast<int>(material.flags) & 0x07)
        | ((static_cast<int>(material.cornerStyle) & 0x1F) << 3)
        | (static_cast<int>(material.edgeMask & 0xFFFFu) << 8);
    const float materialFlags = static_cast<float>(packedFlags);
    const float transitionMode = static_cast<float>(
        static_cast<int>(material.transition.layer));
    const float transitionAxis = static_cast<float>(material.transition.axisOrSideMask);
    const float transitionJitter = material.transition.jitter;
    const float transitionOrigin = material.transition.tangentOrigin;

    // Per-corner atlas UV under an orientation index 0..7 (bits 0-1: quarter
    // turns of the content, bit 2: horizontal mirror applied first). The
    // GEOMETRY and each vertex's aLocal stay exactly where they were, so edge
    // bands, corner patches, world-anchored detail and everything that reads
    // the tile's frame never notice; only which atlas texel a corner samples
    // permutes. For a gutter-baked seamless ground cell every orientation is
    // still a wrap-exact sample of the same periodic content, which is what
    // lets a tile break its one-frame stamp for the cost of nothing but this
    // index: no extra vertex bandwidth, no new attribute, no shader change.
    //
    // Walk each sink corner back to the source corner it samples: undo the
    // quarter turns ((x, y) -> (-y, x) each step) and then the mirror
    // negation, and take the rect's edge that the resulting sign names.
    const auto cornerUv = [&](float localX, float localY, const UvRect& rect) {
        float sx = localX;
        float sy = localY;
        if (uvOrientation > 0) {
            const int quarter = uvOrientation & 0x03;
            for (int step = 0; step < quarter; ++step) {
                const float px = sx;
                sx = -sy;
                sy = px;
            }
            if ((uvOrientation & 0x04) != 0) {
                sx = -sx;
            }
        }
        return std::pair<float, float>{
            rect.uForLocalX(sx),
            rect.vForLocalY(sy)};
    };
    const auto [sourceU0, sourceV0] = cornerUv(-1.0f, -1.0f, uv);
    const auto [sourceU1, sourceV1] = cornerUv(1.0f, -1.0f, uv);
    const auto [sourceU2, sourceV2] = cornerUv(1.0f, 1.0f, uv);
    const auto [sourceU3, sourceV3] = cornerUv(-1.0f, 1.0f, uv);
    const auto [nextU0, nextV0] = cornerUv(-1.0f, -1.0f, nextFrameUv);
    const auto [nextU1, nextV1] = cornerUv(1.0f, -1.0f, nextFrameUv);
    const auto [nextU2, nextV2] = cornerUv(1.0f, 1.0f, nextFrameUv);
    const auto [nextU3, nextV3] = cornerUv(-1.0f, 1.0f, nextFrameUv);

    const float buildingArrayLayerFloat = static_cast<float>(currentBuildingArrayLayer_);
    const BatchVertex tri0[] = {
        {x0, y0, 0.0f, sourceU0, sourceV0, red, green, blue, alpha, -1.0f, -1.0f, blendRed, blendGreen, blendBlue, blendLeft, blendRight, blendDown, blendUp, kind, wetness, emissive, detailLayer, nextU0, nextV0, frameBlend, detailScale, detailStrength, detailOffsetU, detailOffsetV, occluded, materialFlags, buildingArrayLayerFloat, transitionMode, transitionAxis, transitionJitter, transitionOrigin},
        {x1, y1, 0.0f, sourceU1, sourceV1, red, green, blue, alpha, 1.0f, -1.0f, blendRed, blendGreen, blendBlue, blendLeft, blendRight, blendDown, blendUp, kind, wetness, emissive, detailLayer, nextU1, nextV1, frameBlend, detailScale, detailStrength, detailOffsetU, detailOffsetV, occluded, materialFlags, buildingArrayLayerFloat, transitionMode, transitionAxis, transitionJitter, transitionOrigin},
        {x2, y2, 0.0f, sourceU2, sourceV2, red, green, blue, alpha, 1.0f, 1.0f, blendRed, blendGreen, blendBlue, blendLeft, blendRight, blendDown, blendUp, kind, wetness, emissive, detailLayer, nextU2, nextV2, frameBlend, detailScale, detailStrength, detailOffsetU, detailOffsetV, occluded, materialFlags, buildingArrayLayerFloat, transitionMode, transitionAxis, transitionJitter, transitionOrigin},
        {x0, y0, 0.0f, sourceU0, sourceV0, red, green, blue, alpha, -1.0f, -1.0f, blendRed, blendGreen, blendBlue, blendLeft, blendRight, blendDown, blendUp, kind, wetness, emissive, detailLayer, nextU0, nextV0, frameBlend, detailScale, detailStrength, detailOffsetU, detailOffsetV, occluded, materialFlags, buildingArrayLayerFloat, transitionMode, transitionAxis, transitionJitter, transitionOrigin},
        {x2, y2, 0.0f, sourceU2, sourceV2, red, green, blue, alpha, 1.0f, 1.0f, blendRed, blendGreen, blendBlue, blendLeft, blendRight, blendDown, blendUp, kind, wetness, emissive, detailLayer, nextU2, nextV2, frameBlend, detailScale, detailStrength, detailOffsetU, detailOffsetV, occluded, materialFlags, buildingArrayLayerFloat, transitionMode, transitionAxis, transitionJitter, transitionOrigin},
        {x3, y3, 0.0f, sourceU3, sourceV3, red, green, blue, alpha, -1.0f, 1.0f, blendRed, blendGreen, blendBlue, blendLeft, blendRight, blendDown, blendUp, kind, wetness, emissive, detailLayer, nextU3, nextV3, frameBlend, detailScale, detailStrength, detailOffsetU, detailOffsetV, occluded, materialFlags, buildingArrayLayerFloat, transitionMode, transitionAxis, transitionJitter, transitionOrigin},
    };

    spriteBatch_.insert(spriteBatch_.end(), std::begin(tri0), std::end(tri0));
}

bool Renderer2D::enqueueSprite(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    const Texture2D& texture,
    UvRect uv,
    float red,
    float green,
    float blue,
    float alpha,
    float blendRed,
    float blendGreen,
    float blendBlue,
    float blendLeft,
    float blendRight,
    float blendDown,
    float blendUp,
    MaterialParams2D material,
    UvRect nextFrameUv,
    float frameBlend,
    int uvOrientation) {
    if (!batchActive_ || !hasSpriteBatch() || !texture.valid() || halfWidth <= 0.0f || halfHeight <= 0.0f) {
        return false;
    }

    const unsigned int textureId = texture.id();
    if (batchTextureId_ != 0 && batchTextureId_ != textureId) {
        flushSpriteBatch();
        batchActive_ = true;
    }

    // Like the rotated path below: when CastShadow is combined with the
    // CPU-only noTextureShadow hint, the caller submits a geometric polygon
    // instead, so no automatic texture-shadow copy is emitted. Without this
    // check high-quality trees and buildings cast twice (texture silhouette
    // plus polygon) and read as double shadows.
    const bool castsShadow = shadowEnabled_
        && (static_cast<int>(material.flags) & static_cast<int>(MaterialFlags2D::CastShadow)) != 0
        && !material.noTextureShadow
        && shadowLength_ > 0.0001f
        && shadowA_ > 0.004f;
    const std::size_t neededVertices = castsShadow ? 12 : 6;
    if (spriteBatch_.size() + neededVertices > kMaxBatchVertices) {
        flushSpriteBatch();
        batchActive_ = true;
    }

    batchTextureId_ = textureId;

    // Ground-projected shadow: a darkened copy of the caster stretched away
    // from the light. The bottom stays anchored at the sprite so the caster
    // paints over its near edge; the top is sheared by the shadow length. This
    // reads as a 3D object casting onto the ground rather than a paper cutout.
    // Oblique sun additionally widens the silhouette (shadowWidthScale_).
    if (castsShadow) {
        const float safeAspect = std::max(batchAspect_, 0.0001f);
        // Shadow falls opposite to the light direction.
        const float shadowShearX = -materialLightDirectionX_ * halfHeight * shadowLength_ / safeAspect;
        const float shadowShearY = -materialLightDirectionY_ * halfHeight * shadowLength_;
        MaterialParams2D shadowMaterial = material;
        shadowMaterial.kind = MaterialKind2D::Shadow;
        shadowMaterial.flags = MaterialFlags2D::FlatSurface;
        shadowMaterial.wetness = 0.0f;
        shadowMaterial.emissive = 0.0f;
        shadowMaterial.detail = {};
        shadowMaterial.cornerStyle = 0;
        shadowMaterial.edgeMask = 0;
        appendSpriteQuad(
            x,
            y,
            halfWidth * shadowWidthScale_,
            halfHeight,
            uv,
            shadowR_,
            shadowG_,
            shadowB_,
            shadowA_,
            1.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            shadowMaterial,
            nextFrameUv,
            frameBlend,
            1.0f, 0.0f,
            0.0f, 0.0f,
            shadowShearX,
            shadowShearY,
            uvOrientation);
    }

    appendSpriteQuad(
        x,
        y,
        halfWidth,
        halfHeight,
        uv,
        red,
        green,
        blue,
        alpha,
        blendRed,
        blendGreen,
        blendBlue,
        blendLeft,
        blendRight,
        blendDown,
        blendUp,
        material,
        nextFrameUv,
        frameBlend,
        1.0f, 0.0f,
        0.0f, 0.0f,
        0.0f, 0.0f,
        uvOrientation);
    return true;
}

bool Renderer2D::enqueueSpriteRotated(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float cosAngle,
    float sinAngle,
    const Texture2D& texture,
    UvRect uv,
    float red,
    float green,
    float blue,
    float alpha,
    float blendRed,
    float blendGreen,
    float blendBlue,
    MaterialParams2D material,
    float pivotX,
    float pivotY,
    float taperNear,
    float taperFar,
    float mirrorX) {
    if (!batchActive_ || !hasSpriteBatch() || !texture.valid() || halfWidth <= 0.0f || halfHeight <= 0.0f) {
        return false;
    }

    const unsigned int textureId = texture.id();
    if (batchTextureId_ != 0 && batchTextureId_ != textureId) {
        flushSpriteBatch();
        batchActive_ = true;
    }

    const bool castsTextureShadow = shadowEnabled_
        && (static_cast<int>(material.flags) & static_cast<int>(MaterialFlags2D::CastShadow)) != 0
        && !material.noTextureShadow
        && shadowLength_ > 0.0001f
        && shadowA_ > 0.004f;
    const std::size_t neededVertices = castsTextureShadow ? 12 : 6;
    if (spriteBatch_.size() + neededVertices > kMaxBatchVertices) {
        flushSpriteBatch();
        batchActive_ = true;
    }

    batchTextureId_ = textureId;

    // Ground-projected shadow: a darkened copy of the caster stretched away
    // from the light. The bottom stays anchored at the caster while the top
    // stretches away from the light. This
    // reads as a 3D object casting onto the ground rather than a paper cutout.
    // Oblique sun additionally widens the silhouette (shadowWidthScale_).
    if (castsTextureShadow) {
        batchHasShadowCopies_ = true;
        const float safeAspect = std::max(batchAspect_, 0.0001f);
        const float shadowShearX = -materialLightDirectionX_ * halfHeight * shadowLength_ / safeAspect;
        const float shadowShearY = -materialLightDirectionY_ * halfHeight * shadowLength_;
        MaterialParams2D shadowMaterial = material;
        shadowMaterial.kind = MaterialKind2D::Shadow;
        shadowMaterial.flags = MaterialFlags2D::FlatSurface;
        shadowMaterial.wetness = 0.0f;
        shadowMaterial.emissive = 0.0f;
        shadowMaterial.detail = {};
        shadowMaterial.cornerStyle = 0;
        shadowMaterial.edgeMask = 0;
        appendSpriteQuad(
            x,
            y,
            halfWidth * shadowWidthScale_,
            halfHeight,
            uv,
            shadowR_,
            shadowG_,
            shadowB_,
            shadowA_,
            1.0f, 1.0f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            shadowMaterial,
            uv,
            0.0f,
            cosAngle,
            sinAngle,
            pivotX,
            pivotY,
            shadowShearX,
            shadowShearY,
            0,
            taperNear,
            taperFar,
            mirrorX);
    }

    appendSpriteQuad(
        x,
        y,
        halfWidth,
        halfHeight,
        uv,
        red,
        green,
        blue,
        alpha,
        blendRed,
        blendGreen,
        blendBlue,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        material,
        uv,
        0.0f,
        cosAngle,
        sinAngle,
        pivotX,
        pivotY,
        0.0f,
        0.0f,
        0,
        taperNear,
        taperFar,
        mirrorX);
    return true;
}

void Renderer2D::ensureBatchBuffer(std::size_t vertexCount) {
    const std::size_t needed = vertexCount * sizeof(BatchVertex);
    if (needed <= batchBufferBytes_) {
        return;
    }
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(needed), nullptr, GL_DYNAMIC_DRAW);
    batchBufferBytes_ = needed;
}

void Renderer2D::flushSpriteBatch() {
    if (!batchActive_ || !hasSpriteBatch()) {
        spriteBatch_.clear();
        batchTextureId_ = 0;
        batchActive_ = false;
        batchHasShadowCopies_ = false;
        shadowCasters_ = nullptr;
        contactShadowCasters_ = nullptr;
        contactShadowAlpha_ = 0.0f;
        return;
    }
    const bool hasSunCasters = shadowCasters_ != nullptr
        && !shadowCasters_->polygons().empty();
    const bool hasContactCasters = contactShadowCasters_ != nullptr
        && !contactShadowCasters_->polygons().empty()
        && contactShadowAlpha_ > 0.004f;
    if (spriteBatch_.empty() && !hasSunCasters && !hasContactCasters) {
        batchTextureId_ = 0;
        batchActive_ = false;
        return;
    }

    // Empty flushes return above; everything below is one real submission.
    ++spriteBatchFlushes_;
    glBindVertexArray(batchVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, batchVbo_->id());
    // Orphan the store before uploading: the GPU may still be reading the
    // previous batch, and SubData into a busy store stalls until it drains.
    // The orphan is sized to what this flush actually carries, not to the
    // buffer's maximum: drivers treat a re-specify as allocate-plus-copy, and
    // on Adreno reserving the full 24k-vertex store on every flush made the
    // driver memmove megabytes per bucket (that was 40 ms of a world frame).
    const std::size_t batchBytes = spriteBatch_.size() * sizeof(BatchVertex);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(batchBytes),
        nullptr,
        GL_DYNAMIC_DRAW);
    batchBufferBytes_ = batchBytes;
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(spriteBatch_.size() * sizeof(BatchVertex)),
        spriteBatch_.data());

    // Remember the destination we are actually drawing into so the shadow
    // passes can bind their own FBOs and we can restore afterwards.
    //
    // Queried ONCE PER FRAME, not once per flush. Two glGetIntegerv calls cost
    // 0.44 ms of CPU - each one is a driver round-trip that has to drain the
    // command stream before it answers - and a world frame costs fourteen
    // flushes, so asking every time burned six milliseconds a frame on state
    // the renderer already knew. Within one frame the destination is constant:
    // Renderer2D is the only thing in the engine that binds a framebuffer, and
    // every one of its own binds goes through noteRenderTargetChanged(), so a
    // flush always sees either the default framebuffer or the one dynamic
    // resolution target it has been drawing into all frame.
    if (!renderTargetKnown_) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &renderTargetFbo_);
        glGetIntegerv(GL_VIEWPORT, renderTargetViewport_);
        renderTargetKnown_ = true;
    }
    const GLint previousFbo = renderTargetFbo_;
    const GLint previousViewport[4] = {
        renderTargetViewport_[0],
        renderTargetViewport_[1],
        renderTargetViewport_[2],
        renderTargetViewport_[3]};

    const bool hasSunPass = shadowEnabled_
        && shadowMap_.valid()
        && shadowBlur_.valid()
        && shadowRaw_.valid();
    const bool hasShadowPass = hasSunPass || hasContactCasters;

    // Shadow cache: skip re-rendering the sun shadow map when the caster set,
    // frame, resolution, and shadow enabled state haven't changed AND the batch
    // about to draw carries no new texture-shadow copies. The sprite-copy path
    // rasterises the current batch only, so a flush that brings new copies must
    // repaint the map to reach them; skipping here left those casters' copies
    // discarded by the lit pass without ever entering the map, which silently
    // dropped their shadows (this is what the player silhouette probe caught).
    // Polygon casters are frozen before the first sprite, so a copy-free flush
    // can safely reuse the map: the polygons redraw identically anyway.
    const bool shadowCacheMatches = (cachedShadowCasters_ == shadowCasters_)
        && (cachedShadowFrame_ == frameIndex_)
        && (cachedShadowWidth_ == shadowBufferWidth_)
        && (cachedShadowHeight_ == shadowBufferHeight_)
        && (cachedShadowEnabled_ == shadowEnabled_);
    const bool useCachedShadowMap = hasSunPass
        && shadowCacheMatches
        && !batchHasShadowCopies_;

    if (hasShadowPass) {
        if (!useCachedShadowMap) {
            // Incremental when only the copy-arrival bit differs: the raw map
            // already holds this frame's earlier batches and polygons, so the
            // append adds exactly the new copies. Besides skipping the polygon
            // re-rasterisation and the clear, this fixes a coverage gap: the
            // old clear+redraw-current-batch path dropped every earlier flush's
            // copies from the map whenever one frame split into several flushes.
            renderShadowMapPass(shadowCacheMatches);
            if (hasSunPass) {
                blurShadowMap();
            }
            // Update cache after generating a fresh shadow map.
            cachedShadowCasters_ = shadowCasters_;
            cachedShadowFrame_ = frameIndex_;
            cachedShadowWidth_ = shadowBufferWidth_;
            cachedShadowHeight_ = shadowBufferHeight_;
            cachedShadowEnabled_ = shadowEnabled_;
        }
        // The contact pass runs after the sun pass, and after the blur when
        // there is a sun throw, on purpose: a sole patch is small enough that
        // the 9-tap Gaussian turns it into a soft smudge, and the hard edge is
        // the part that reads as ground contact. It still shares the same map
        // and MAX blend, so a darker sun shadow wins where the two overlap.
        // With no sun throw there is nothing to blur, so the map is already
        // clear when this runs.
        renderContactShadowPass();
        // Restore the destination framebuffer and viewport for the lit pass.
        glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        // blurShadowMap bound the full-screen quad VAO; rebind the batch VAO
        // so the lit pass reads the sprite vertex data.
        glBindVertexArray(batchVao_->id());
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, spriteAdditive_ ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);

    ShaderProgram& shader = shaders_.program(ShaderId::Textured2DBatch);
    shader.use();
    shader.setInt("u_texture", 0);
    shader.setInt("u_normal_texture", 1);
    shader.setInt("u_detail_texture", 2);
    shader.setInt("u_shadow_texture", 3);
    shader.setInt("u_material_mask_texture", 4);
    // Unit 5 is where the building array is bound below. It MUST be assigned:
    // a sampler uniform defaults to unit 0, and unit 0 already holds the
    // sampler2D world atlas. A texture unit read by two samplers of different
    // types makes the whole draw an INVALID_OPERATION on GLES, which drops the
    // entire sprite batch silently - the desktop driver tolerates it, which is
    // why this only showed up on Android.
    shader.setInt("u_building_array", 5);
    shader.setInt("u_shadow_enabled", hasShadowPass ? 1 : 0);
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);
    shader.setFloat("u_aspect", std::max(batchAspect_, 0.0001f));
    shader.setVec2("u_detail_origin", detailOriginX_, detailOriginY_);
    shader.setVec2("u_light_direction", materialLightDirectionX_, materialLightDirectionY_);
    // The boundary treatment table. It comes from the caller, not from a
    // function in the shader, because the materials are the caller's: a game
    // that adds a rock type should not have to edit the library's GLSL to give
    // it an edge.
    if (edgeStyleTableDirty_) {
        // The entries are a seven-float struct, which is not a layout an
        // upload can describe: a uniform array is tightly packed, a struct is
        // not. So the table is unpacked into three plain vec3 arrays here,
        // once per change rather than once per flush.
        float styles[kEdgeStyleTableSize * 3];
        float radii[kEdgeStyleTableSize * 3];
        float tints[kEdgeStyleTableSize * 3];
        for (int index = 0; index < kEdgeStyleTableSize; ++index) {
            const EdgeStyleEntry& entry = edgeStyleTable_.entry[index];
            styles[index * 3 + 0] = entry.bandWidth;
            styles[index * 3 + 1] = entry.darken;
            styles[index * 3 + 2] = entry.bevel;
            radii[index * 3 + 0] = entry.radius;
            radii[index * 3 + 1] = 0.0f;
            radii[index * 3 + 2] = 0.0f;
            tints[index * 3 + 0] = entry.tintR;
            tints[index * 3 + 1] = entry.tintG;
            tints[index * 3 + 2] = entry.tintB;
        }
        shader.setVec3Array("u_edge_style", kEdgeStyleTableSize, styles);
        shader.setVec3Array("u_edge_radius", kEdgeStyleTableSize, radii);
        shader.setVec3Array("u_edge_tint", kEdgeStyleTableSize, tints);
        shader.setFloat("u_edge_tint_strength", edgeStyleTable_.tintStrength);
        edgeStyleTableDirty_ = false;
    }
    shader.setVec3("u_ambient_color", ambientR_, ambientG_, ambientB_);
    shader.setFloat("u_ambient_intensity", ambientIntensity_);
    shader.setFloat("u_daylight", std::clamp(daylightFactor_, 0.0f, 1.0f));
    shader.setFloat("u_aoStrength", aoStrength_);
    shader.setFloat("u_sssStrength", sssStrength_);
    shader.setVec3("u_sssColor", sssColorR_, sssColorG_, sssColorB_);
    shader.setVec3("u_fogColor", fogR_, fogG_, fogB_);
    shader.setFloat("u_fogDensity", fogDensity_);
    shader.setVec2("u_fogCenter", fogCenterX_, fogCenterY_);
    shader.setVec2("u_fogWorldScale", fogWorldScaleX_, fogWorldScaleY_);
    shader.setFloat("u_vignette", vignetteStrength_);

    const bool hasNormalMap = spriteNormalTexture_ != nullptr && spriteNormalTexture_->valid();
    shader.setInt("u_has_normal_map", hasNormalMap ? 1 : 0);

    const bool hasDetailMap = spriteDetailTexture_ != nullptr && spriteDetailTexture_->valid();
    shader.setInt("u_has_detail_map", hasDetailMap ? 1 : 0);
    shader.setInt(
        "u_detail_layers",
        hasDetailMap ? std::max(spriteDetailTexture_->layers(), 1) : 1);

    // Building texture array
    const bool hasBuildingArray = buildingTextureArray_ != nullptr && buildingTextureArray_->valid();
    shader.setInt("u_has_building_array", hasBuildingArray ? 1 : 0);

    // Per-part material mask for the texture being drawn, if one was paired
    // with it. Looked up rather than handed over per sprite: the batch breaks on
    // texture change by itself, so the pairing is a property of the texture and
    // this costs one hash lookup per draw call.
    const Texture2D* materialMask = nullptr;
    {
        const auto paired = spriteMaterialMasks_.find(batchTextureId_);
        if (paired != spriteMaterialMasks_.end()) {
            materialMask = paired->second;
        }
    }
    const bool hasMaterialMask = materialMask != nullptr && materialMask->valid();
    shader.setInt("u_has_material_mask", hasMaterialMask ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, batchTextureId_);

    if (hasNormalMap) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, spriteNormalTexture_->id());
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasDetailMap) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, spriteDetailTexture_->id());
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasBuildingArray) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, buildingTextureArray_->id());
        glActiveTexture(GL_TEXTURE0);
    }

    if (hasShadowPass) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, shadowMap_.colorTexture().id);
        glActiveTexture(GL_TEXTURE0);
    } else {
        // Bind something valid to unit 3 even when shadows are off so the
        // shader sampler is never unbound; the branch will not sample it.
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, batchTextureId_);
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasMaterialMask) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, materialMask->id());
        glActiveTexture(GL_TEXTURE0);
    }

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(spriteBatch_.size()));
    if (hasNormalMap) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasDetailMap) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasBuildingArray) {
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasShadowPass) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    if (hasMaterialMask) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }
    glBindVertexArray(0);

    spriteBatch_.clear();
    batchTextureId_ = 0;
    batchActive_ = false;
    batchHasShadowCopies_ = false;
    currentBuildingArrayLayer_ = -1;
}

void Renderer2D::renderShadowMapPass(bool incremental) {
    shadowRaw_.bind();
    if (!incremental) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // The blur fully rewrites the display map from the raw one, but with
        // no sun pass there is no blur - clear the display map here too or a
        // contact-only frame would keep last frame's patches.
        if (shadowMap_.valid()) {
            shadowMap_.bind();
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            shadowRaw_.bind();
        }
    }

    // The sprite-copy path and the sun polygon set only exist when the sun
    // throw is enabled. At noon the contact pass still needs a cleared map,
    // but drawing the whole world batch through the shadow shader just to
    // discard every fragment is pure cost; skip it.
    if (shadowEnabled_) {
        // Darkest-wins accumulation: every caster writes its darkness and the
        // MAX equation keeps the darkest single value per pixel. Overlapping
        // shadows (a dense forest, a house under a tree) therefore read as one
        // shadow instead of multiplying toward black. GL_MAX is core on every
        // target we ship (desktop GL 3.3, Android GLES 3.x). MAX is also what
        // makes the incremental append exact: re-drawn fragments are no-ops.
        glEnable(GL_BLEND);
        glBlendEquation(GL_MAX);
        glBlendFunc(GL_ONE, GL_ONE);

        ShaderProgram& shader = shaders_.program(ShaderId::ShadowMap);
        shader.use();
        shader.setInt("u_texture", 0);
        shader.setFloat("u_aspect", std::max(batchAspect_, 0.0001f));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, batchTextureId_);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(spriteBatch_.size()));

        // The polygons are frozen for the whole frame, so an incremental
        // render finds them already in the raw map and must not pay for them
        // again.
        if (!incremental) {
            renderPolygonShadowPass();
        }
    }

    // Every later pass in this engine assumes additive blending; restore it
    // here so the blur and lit passes below need no knowledge of shadows.
    glBlendEquation(GL_FUNC_ADD);
}

// Screen-cull a shadow polygon before triangulation. Polygon vertices live in
// the batch's viewport space (x in [-aspect, aspect], y in [-1, 1] covers the
// screen; see shadow_polygon.vert), and the polygon already includes its full
// throw, so a bounds test against that rect is exact: a shadow whose hull
// misses the screen can never darken a visible pixel. Off-screen casters would
// otherwise still pay triangulation, vertex upload and shadow-map fill.
static bool shadowPolygonOnScreen(const std::vector<ShadowVertex2D>& vertices, float aspect) {
    const float boundX = std::max(aspect, 0.0001f);
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const ShadowVertex2D& v : vertices) {
        minX = std::min(minX, v.x);
        minY = std::min(minY, v.y);
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
    }
    return maxX >= -boundX && minX <= boundX && maxY >= -1.0f && minY <= 1.0f;
}

void Renderer2D::renderPolygonShadowPass() {
    // The caster set is NOT consumed here: it stays valid for every flush of
    // the frame (mid-frame texture-switch flushes must see the same polygons
    // as the final flush), and beginFrame() clears it for the next frame.
    // The set is frozen once submission starts - WorldRenderer completes it
    // before the first flush - so the triangulation cache below stays valid.
    if (shadowCasters_ == nullptr || shadowCasters_->polygons().empty()) {
        return;
    }
    if (polygonShadowVao_ == nullptr || polygonShadowVbo_ == nullptr) {
        return;
    }

    // Fan once per caster set, not once per flush: the vertex buffer keeps
    // the triangulation and only re-uploads on a cache miss. The aspect is
    // part of the key because screen culling bakes the viewport width into
    // the kept vertex set.
    if (triangulatedCasters_ != shadowCasters_ || triangulatedCastersAspect_ != batchAspect_) {
        polygonShadowVertices_.clear();
        for (const auto& polygon : shadowCasters_->polygons()) {
            const auto& vertices = polygon.vertices;
            const std::size_t count = vertices.size();
            if (count < 3) {
                continue;
            }
            if (!shadowPolygonOnScreen(vertices, batchAspect_)) {
                continue;
            }
            for (std::size_t i = 1; i + 1 < count; ++i) {
                // Per-vertex diagonal weight times the polygon's own alpha: a
                // caster that is intrinsically faint stays faint, and only the
                // falloff along the throw is per-vertex.
                const float pa = polygon.alpha;
                polygonShadowVertices_.push_back(
                    {vertices[0].x, vertices[0].y, vertices[0].alpha * pa});
                polygonShadowVertices_.push_back(
                    {vertices[i].x, vertices[i].y, vertices[i].alpha * pa});
                polygonShadowVertices_.push_back(
                    {vertices[i + 1].x, vertices[i + 1].y, vertices[i + 1].alpha * pa});
            }
        }
        triangulatedCasters_ = shadowCasters_;
        triangulatedCastersAspect_ = batchAspect_;
        if (!polygonShadowVertices_.empty()) {
            glBindVertexArray(polygonShadowVao_->id());
            glBindBuffer(GL_ARRAY_BUFFER, polygonShadowVbo_->id());
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(polygonShadowVertices_.size() * sizeof(PolygonShadowVertex)),
                polygonShadowVertices_.data(),
                GL_DYNAMIC_DRAW);
        }
    }

    if (polygonShadowVertices_.empty()) {
        return;
    }

    ShaderProgram& shader = shaders_.program(ShaderId::ShadowPolygon);
    shader.use();
    shader.setFloat("u_aspect", std::max(batchAspect_, 0.0001f));
    shader.setFloat("u_shadow_alpha", shadowA_);

    glDisable(GL_CULL_FACE);
    glBindVertexArray(polygonShadowVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, polygonShadowVbo_->id());

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 1, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, alpha)));
    glEnableVertexAttribArray(1);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(polygonShadowVertices_.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer2D::renderContactShadowPass() {
    if (contactShadowCasters_ == nullptr
        || contactShadowCasters_->polygons().empty()
        || contactShadowAlpha_ <= 0.004f
        || contactShadowVao_ == nullptr
        || contactShadowVbo_ == nullptr) {
        return;
    }

    // This pass deliberately runs after the sun pass and, when the sun is
    // enabled, after blurShadowMap(). The sun throw wants the soft 9-tap edge;
    // a boot contact patch does not. Re-triangulate only when the caller hands
    // over a different set, exactly like the sun pass. Same screen cull too:
    // the aspect is part of the cache key for the reason given there.
    if (triangulatedContactCasters_ != contactShadowCasters_
        || triangulatedContactCastersAspect_ != batchAspect_) {
        contactShadowVertices_.clear();
        for (const auto& polygon : contactShadowCasters_->polygons()) {
            const auto& vertices = polygon.vertices;
            const std::size_t count = vertices.size();
            if (count < 3) {
                continue;
            }
            if (!shadowPolygonOnScreen(vertices, batchAspect_)) {
                continue;
            }
            for (std::size_t i = 1; i + 1 < count; ++i) {
                const float pa = polygon.alpha;
                contactShadowVertices_.push_back(
                    {vertices[0].x, vertices[0].y, vertices[0].alpha * pa});
                contactShadowVertices_.push_back(
                    {vertices[i].x, vertices[i].y, vertices[i].alpha * pa});
                contactShadowVertices_.push_back(
                    {vertices[i + 1].x, vertices[i + 1].y, vertices[i + 1].alpha * pa});
            }
        }
        triangulatedContactCasters_ = contactShadowCasters_;
        triangulatedContactCastersAspect_ = batchAspect_;
        if (!contactShadowVertices_.empty()) {
            glBindVertexArray(contactShadowVao_->id());
            glBindBuffer(GL_ARRAY_BUFFER, contactShadowVbo_->id());
            glBufferData(
                GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(
                    contactShadowVertices_.size() * sizeof(PolygonShadowVertex)),
                contactShadowVertices_.data(),
                GL_DYNAMIC_DRAW);
        }
    }

    if (contactShadowVertices_.empty()) {
        return;
    }

    // blurShadowMap() left the shadow map bound and blend disabled. Bind it
    // again so the viewport and target are explicit, then restore MAX blending
    // for the same darkest-wins accumulation the sun pass uses.
    shadowMap_.bind();
    glEnable(GL_BLEND);
    glBlendEquation(GL_MAX);
    glBlendFunc(GL_ONE, GL_ONE);

    ShaderProgram& shader = shaders_.program(ShaderId::ShadowPolygon);
    shader.use();
    shader.setFloat("u_aspect", std::max(batchAspect_, 0.0001f));
    shader.setFloat("u_shadow_alpha", contactShadowAlpha_);

    glDisable(GL_CULL_FACE);
    glBindVertexArray(contactShadowVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, contactShadowVbo_->id());

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 1, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, alpha)));
    glEnableVertexAttribArray(1);

    glDrawArrays(
        GL_TRIANGLES, 0, static_cast<GLsizei>(contactShadowVertices_.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    // Every later pass assumes additive blending; restore it here so the
    // caller's lit pass needs no knowledge of the contact pass.
    glBlendEquation(GL_FUNC_ADD);
}

void Renderer2D::blurShadowMap() {
    ShaderProgram& shader = shaders_.program(ShaderId::ShadowBlur);
    shader.use();
    shader.setInt("u_texture", 0);
    shader.setVec2(
        "u_texel",
        1.0f / std::max(static_cast<float>(shadowBufferWidth_), 1.0f),
        1.0f / std::max(static_cast<float>(shadowBufferHeight_), 1.0f));
    // Pass the quality preset's shadow resolution scale so the blur kernel
    // adapts to half/full resolution shadow maps.
    shader.setFloat("u_shadowResolutionScale",
        qualityPreset(qualityTier_).shadowResolutionScale);

    // Blur passes should overwrite, not blend with the clear colour.
    glDisable(GL_BLEND);

    glBindVertexArray(texturedVao_->id());

    // Horizontal pass: shadowRaw_ -> shadowBlur_. The raw accumulation is the
    // blur's source so the display map can be rebuilt from scratch after an
    // incremental append without re-blurring already-blurred content.
    shadowBlur_.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowRaw_.colorTexture().id);
    shader.setVec2("u_direction", 1.0f, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Vertical pass: shadowBlur_ -> shadowMap_
    shadowMap_.bind();
    glBindTexture(GL_TEXTURE_2D, shadowBlur_.colorTexture().id);
    shader.setVec2("u_direction", 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer2D::beginSdfTextBatch(float framebufferAspect) {
    flushSpriteBatch();
    flushRectBatch();
    sdfBatchAspect_ = framebufferAspect;
    sdfTextBatch_.clear();
    sdfBatchActive_ = hasSdfTextBatch();
}

void Renderer2D::appendSdfQuad(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    UvRect uv,
    float red,
    float green,
    float blue,
    float alpha) {
    const float safeAspect = std::max(sdfBatchAspect_, 0.0001f);
    const auto clipCorner = [&](float localX, float localY) -> std::pair<float, float> {
        const float px = localX * halfWidth + x;
        const float py = localY * halfHeight + y;
        return {px / safeAspect, py};
    };

    const auto [x0, y0] = clipCorner(-1.0f, -1.0f);
    const auto [x1, y1] = clipCorner(1.0f, -1.0f);
    const auto [x2, y2] = clipCorner(1.0f, 1.0f);
    const auto [x3, y3] = clipCorner(-1.0f, 1.0f);

    const BatchVertex tri0[] = {
        {x0, y0, 0.0f, uv.u0, uv.v1, red, green, blue, alpha},
        {x1, y1, 0.0f, uv.u1, uv.v1, red, green, blue, alpha},
        {x2, y2, 0.0f, uv.u1, uv.v0, red, green, blue, alpha},
        {x0, y0, 0.0f, uv.u0, uv.v1, red, green, blue, alpha},
        {x2, y2, 0.0f, uv.u1, uv.v0, red, green, blue, alpha},
        {x3, y3, 0.0f, uv.u0, uv.v0, red, green, blue, alpha},
    };

    sdfTextBatch_.insert(sdfTextBatch_.end(), std::begin(tri0), std::end(tri0));
}

bool Renderer2D::enqueueSdfGlyph(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    UvRect uv,
    float red,
    float green,
    float blue,
    float alpha) {
    if (!sdfBatchActive_ || !hasSdfTextBatch() || halfWidth <= 0.0f || halfHeight <= 0.0f) {
        return false;
    }

    if (sdfTextBatch_.size() + 6 > kMaxBatchVertices) {
        flushSdfTextBatch();
        sdfBatchActive_ = true;
    }

    appendSdfQuad(x, y, halfWidth, halfHeight, uv, red, green, blue, alpha);
    return true;
}

void Renderer2D::flushSdfTextBatch() {
    if (!sdfBatchActive_ || sdfTextBatch_.empty() || !hasSdfTextBatch()) {
        sdfTextBatch_.clear();
        sdfBatchActive_ = false;
        sdfBatchFont_ = nullptr;
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(batchVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, batchVbo_->id());
    ensureBatchBuffer(sdfTextBatch_.size());
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(sdfTextBatch_.size() * sizeof(BatchVertex)),
        sdfTextBatch_.data());

    ShaderProgram& shader = shaders_.program(ShaderId::SdfTextBatch);
    shader.use();
    shader.setInt("u_texture", 0);
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);
    const SdfFont* metrics = metricsFont();
    const SdfFont& batchFont = sdfBatchFont_ != nullptr
        ? *sdfBatchFont_
        : (metrics != nullptr ? *metrics : emptyFace());
    shader.setFloat("u_sdfEdge", batchFont.sdfEdge());
    shader.setFloat("u_sdfSmoothing", std::max(0.003f, sdfBatchScale_ * 1.25f));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, batchFont.texture().id());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(sdfTextBatch_.size()));
    ++sdfBatchFlushes_;
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);

    sdfTextBatch_.clear();
    sdfBatchActive_ = false;
    sdfBatchFont_ = nullptr;
}

void Renderer2D::drawSquare(float x, float y, float halfSize, float framebufferAspect, float red, float green, float blue) {
    drawRect(x, y, halfSize, halfSize, framebufferAspect, red, green, blue);
}

void Renderer2D::drawRect(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float framebufferAspect,
    float red,
    float green,
    float blue) {
    if (!hasDeviceResources()) {
        return;
    }

    if (enqueueRectQuad(x, y, halfWidth, halfHeight, framebufferAspect, red, green, blue, 1.0f)) {
        return;
    }

    if (batchActive_) {
        flushSpriteBatch();
    }
    if (rectBatchActive_) {
        flushRectBatch();
    }

    glDisable(GL_BLEND);

    ShaderProgram& shader = shaders_.program(ShaderId::Rect2D);
    shader.use();
    shader.setFloat("u_position_x", x);
    shader.setFloat("u_position_y", y);
    shader.setFloat("u_half_width", halfWidth);
    shader.setFloat("u_half_height", halfHeight);
    shader.setFloat("u_aspect", framebufferAspect);
    shader.setFloat("u_color_r", red);
    shader.setFloat("u_color_g", green);
    shader.setFloat("u_color_b", blue);
    shader.setFloat("u_alpha", 1.0f);
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);

    glBindVertexArray(squareVao_->id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer2D::drawRectAlpha(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float framebufferAspect,
    float red,
    float green,
    float blue,
    float alpha) {
    if (!hasDeviceResources() || alpha <= 0.0f) {
        return;
    }

    if (enqueueRectQuad(x, y, halfWidth, halfHeight, framebufferAspect, red, green, blue, alpha)) {
        return;
    }

    if (batchActive_) {
        flushSpriteBatch();
    }
    if (rectBatchActive_) {
        flushRectBatch();
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ShaderProgram& shader = shaders_.program(ShaderId::Rect2D);
    shader.use();
    shader.setFloat("u_position_x", x);
    shader.setFloat("u_position_y", y);
    shader.setFloat("u_half_width", halfWidth);
    shader.setFloat("u_half_height", halfHeight);
    shader.setFloat("u_aspect", framebufferAspect);
    shader.setFloat("u_color_r", red);
    shader.setFloat("u_color_g", green);
    shader.setFloat("u_color_b", blue);
    shader.setFloat("u_alpha", alpha);
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);

    glBindVertexArray(squareVao_->id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void Renderer2D::drawTexturedRect(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float framebufferAspect,
    const Texture2D& texture,
    float red,
    float green,
    float blue,
    float alpha) {
    drawSprite(x, y, halfWidth, halfHeight, framebufferAspect, texture, {0.0f, 0.0f, 1.0f, 1.0f}, red, green, blue, alpha);
}

bool Renderer2D::ensureBackdropTexture(int framebufferWidth, int framebufferHeight) {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }

    if (backdropTextureId_ == 0) {
        glGenTextures(1, &backdropTextureId_);
    }
    if (backdropTextureId_ == 0) {
        return false;
    }

    glBindTexture(GL_TEXTURE_2D, backdropTextureId_);
    // Mipmapped minification: the panel shader samples a mip directly instead
    // of a 9x9 tap loop, so the min filter reads down the pyramid.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (backdropTextureWidth_ != framebufferWidth || backdropTextureHeight_ != framebufferHeight) {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            framebufferWidth,
            framebufferHeight,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        backdropTextureWidth_ = framebufferWidth;
        backdropTextureHeight_ = framebufferHeight;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

void Renderer2D::releaseShadowBuffers() {
    shadowMap_.destroy();
    shadowBlur_.destroy();
    shadowRaw_.destroy();
    shadowBufferWidth_ = 0;
    shadowBufferHeight_ = 0;
    // Invalidate shadow cache since the buffers are gone.
    cachedShadowCasters_ = nullptr;
    cachedShadowFrame_ = 0;
    cachedShadowWidth_ = 0;
    cachedShadowHeight_ = 0;
    cachedShadowEnabled_ = false;
}

bool Renderer2D::ensureShadowBuffers(int framebufferWidth, int framebufferHeight) {
    if (!hasDeviceResources() || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return false;
    }
    // Shadow-map resolution follows the quality tier: half resolution at the
    // low/medium tiers where the sun blur hides the coarser texels, full
    // resolution at high/ultra so the hard contact patches stay crisp. The
    // scale is clamped so a preset cannot request a buffer larger than the
    // framebuffer or one so small the blur undersamples the casters.
    const float scale = std::clamp(qualityPreset(qualityTier_).shadowResolutionScale, 0.25f, 1.0f);
    const int targetW = std::max(2, static_cast<int>(framebufferWidth * scale));
    const int targetH = std::max(2, static_cast<int>(framebufferHeight * scale));
    if (shadowMap_.valid()
        && shadowMap_.width() == targetW
        && shadowMap_.height() == targetH) {
        return true;
    }

    releaseShadowBuffers();
    // Kept RGBA8: the shadow shader writes a vec4 coverage and a single-channel
    // attachment would discard the carried alpha; the bandwidth saving on the
    // world albedo/normal maps is where it matters.
    if (!shadowMap_.create(targetW, targetH, RenderTargetFormat::Rgba8, 0)) {
        return false;
    }
    if (!shadowBlur_.create(targetW, targetH, RenderTargetFormat::Rgba8, 0)) {
        shadowMap_.destroy();
        return false;
    }
    if (!shadowRaw_.create(targetW, targetH, RenderTargetFormat::Rgba8, 0)) {
        shadowMap_.destroy();
        shadowBlur_.destroy();
        return false;
    }
    // Clamp to edge so shadows at the screen border do not wrap around.
    glBindTexture(GL_TEXTURE_2D, shadowMap_.colorTexture().id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, shadowBlur_.colorTexture().id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, shadowRaw_.colorTexture().id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    shadowBufferWidth_ = targetW;
    shadowBufferHeight_ = targetH;
    return true;
}

bool Renderer2D::captureBackbufferForBlur(int framebufferWidth, int framebufferHeight) {
    if (!hasDeviceResources() || !ensureBackdropTexture(framebufferWidth, framebufferHeight)) {
        backdropCaptured_ = false;
        return false;
    }

    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();

    // Drain errors raised by earlier, unrelated draws: without this, a stale
    // error flag would fail this frame's capture and the glass panels would
    // lose their blurred backing for a frame (visible flicker). Bounded so a
    // pathological driver can never stall the frame here.
    for (int drain = 0; drain < 16 && glGetError() != GL_NO_ERROR; ++drain) {
    }

    glBindTexture(GL_TEXTURE_2D, backdropTextureId_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, framebufferWidth, framebufferHeight);
    // Build the mip pyramid the frosted-glass shader samples: one driver-side
    // build replaces the old 81-tap convolution per panel pixel.
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        // A failed copy leaves the texture untouched, so keep showing the
        // last good capture instead of blinking the glass panels.
        return backdropCaptured_;
    }
    backdropCaptured_ = true;
    return true;
}

bool Renderer2D::drawCapturedBlurredRect(
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
    float radiusPixels) {
    if (!hasDeviceResources()
        || !backdropCaptured_
        || backdropTextureId_ == 0
        || halfWidth <= 0.0f
        || halfHeight <= 0.0f
        || alpha <= 0.0f
        || framebufferWidth <= 0
        || framebufferHeight <= 0) {
        return false;
    }

    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ShaderProgram& shader = shaders_.program(ShaderId::BackdropBlur);
    shader.use();
    shader.setFloat("u_position_x", x);
    shader.setFloat("u_position_y", y);
    shader.setFloat("u_half_width", halfWidth);
    shader.setFloat("u_half_height", halfHeight);
    shader.setFloat("u_aspect", framebufferAspect);
    shader.setFloat("u_framebuffer_width", static_cast<float>(framebufferWidth));
    shader.setFloat("u_framebuffer_height", static_cast<float>(framebufferHeight));
    shader.setFloat("u_radius_pixels", std::max(0.0f, radiusPixels));
    shader.setFloat("u_tint_r", red);
    shader.setFloat("u_tint_g", green);
    shader.setFloat("u_tint_b", blue);
    shader.setFloat("u_alpha", alpha);
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);
    shader.setInt("u_texture", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, backdropTextureId_);
    glBindVertexArray(squareVao_->id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    const bool drawOk = glGetError() == GL_NO_ERROR;
    // The blur itself is an immediate draw, so it flushes (kills) the rect
    // batch above. Panel draws after the blur (glass tint, borders, grain)
    // expect batching to still work; without resuming here they silently fall
    // back to one GL call per rect, which is the main-menu jank/flicker under
    // load. Re-arm the batch; the caller flushes it at the end of the frame.
    // beginRectBatch() on an empty batch is cheap and safe to call twice.
    beginRectBatch(framebufferAspect);
    return drawOk;
}

bool Renderer2D::drawSprite(
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
    float alpha) {
    return drawTexturedQuad(x, y, halfWidth, halfHeight, framebufferAspect, texture, uv, red, green, blue, alpha);
}

bool Renderer2D::drawTexturedQuad(
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
    float alpha) {
    if (!hasDeviceResources() || !texture.valid() || halfWidth <= 0.0f || halfHeight <= 0.0f) {
        return false;
    }

    if (rectBatchActive_) {
        flushRectBatch();
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, uv.u0, uv.v1,
         1.0f, -1.0f, 0.0f, uv.u1, uv.v1,
         1.0f,  1.0f, 0.0f, uv.u1, uv.v0,

        -1.0f, -1.0f, 0.0f, uv.u0, uv.v1,
         1.0f,  1.0f, 0.0f, uv.u1, uv.v0,
        -1.0f,  1.0f, 0.0f, uv.u0, uv.v0,
    };

    glBindVertexArray(texturedVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, texturedVbo_->id());
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    ShaderProgram& shader = shaders_.program(ShaderId::Textured2D);
    shader.use();
    shader.setFloat("u_position_x", x);
    shader.setFloat("u_position_y", y);
    shader.setFloat("u_half_width", halfWidth);
    shader.setFloat("u_half_height", halfHeight);
    shader.setFloat("u_aspect", framebufferAspect);
    shader.setFloat("u_tint_r", red);
    shader.setFloat("u_tint_g", green);
    shader.setFloat("u_tint_b", blue);
    shader.setFloat("u_alpha", alpha);
    shader.setFloat("u_time", shaderTimeSeconds_);
    shader.setFloat("u_effect_quality", effectQuality_);
    shader.setInt("u_texture", 0);
    // Same look as the batched path: uniforms default to 0 in GLSL, which
    // would silently disable AO/SSS/fog on this immediate path.
    shader.setFloat("u_aoStrength", aoStrength_);
    shader.setFloat("u_sssStrength", sssStrength_);
    shader.setVec3("u_sssColor", sssColorR_, sssColorG_, sssColorB_);
    shader.setVec3("u_fogColor", fogR_, fogG_, fogB_);
    shader.setFloat("u_fogDensity", fogDensity_);
    shader.setVec2("u_fogCenter", fogCenterX_, fogCenterY_);
    shader.setVec2("u_fogWorldScale", fogWorldScaleX_, fogWorldScaleY_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture.id());
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    return true;
}

void Renderer2D::setScissorRect(int x, int y, int width, int height) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, width, height);
}

void Renderer2D::clearScissor() {
    glDisable(GL_SCISSOR_TEST);
}

void Renderer2D::endFrame() {
    flushSpriteBatch();
    flushSdfTextBatch();
    flushRectBatch();
    // Safety net for an unbalanced beginTextBatch(): never leak the
    // suspension into the next frame.
    sdfFlushSuspended_ = false;
    glDisable(GL_BLEND);
    clearScissor();
}

bool Renderer2D::enqueueRect(
    float x,
    float y,
    float halfWidth,
    float halfHeight,
    float framebufferAspect,
    float red,
    float green,
    float blue,
    float alpha) {
    return enqueueRectQuad(x, y, halfWidth, halfHeight, framebufferAspect, red, green, blue, alpha);
}

bool Renderer2D::enqueueClipQuad(
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
    float alpha) {
    return enqueueClipQuadInternal(
        x0,
        y0,
        x1,
        y1,
        x2,
        y2,
        x3,
        y3,
        framebufferAspect,
        red,
        green,
        blue,
        alpha);
}

float Renderer2D::uiScaleToSdfScale(float uiScale) const {
    const SdfFont* metrics = metricsFont();
    return uiScaleToSdfScale(uiScale, metrics != nullptr ? *metrics : emptyFace());
}

float Renderer2D::uiScaleToSdfScale(float uiScale, const SdfFont& font) const {
    if (!font.valid()) {
        return uiScale;
    }

    const float lineHeight = font.lineHeightBake();
    if (lineHeight <= 0.0f) {
        return uiScale;
    }

    return uiScale * (SdfFont::kLegacyLineHeight / lineHeight);
}

float Renderer2D::textAscent(float uiScale) const {
    if (!hasAnySdfFont()) {
        return SdfFont::estimatedUiAscent(uiScale);
    }

    const SdfFont* metrics = metricsFont();
    if (metrics == nullptr) {
        return SdfFont::estimatedUiAscent(uiScale);
    }
    return metrics->ascentBake() * uiScaleToSdfScale(uiScale, *metrics);
}

void Renderer2D::drawTextAtBaseline(
    const char* text,
    float x,
    float baselineY,
    float scale,
    float framebufferAspect,
    float red,
    float green,
    float blue) {
    drawText(text, x, baselineY + textAscent(scale), scale, framebufferAspect, red, green, blue);
}

void Renderer2D::drawText(
    const char* text,
    float x,
    float y,
    float scale,
    float framebufferAspect,
    float red,
    float green,
    float blue,
    float alpha) {
    if (text == nullptr || *text == '\0') {
        return;
    }
    if (!hasAnySdfFont() || !hasSdfTextBatch()) {
        static bool logged = false;
        if (!logged) {
            std::fprintf(
                stderr,
                "Renderer2D: drawText unavailable (fontValid=%d sdfBatch=%d)\n",
                hasAnySdfFont() ? 1 : 0,
                hasSdfTextBatch() ? 1 : 0);
            logged = true;
        }
        return;
    }

    const bool restoreRectBatch = rectBatchActive_;
    if (restoreRectBatch) {
        flushRectBatch();
    }

    const float baselineY = y - textAscent(scale);
    float cursorX = x;
    // Inside a beginTextBatch() scope the batch already holds glyphs of a
    // font, and that font is the right starting point for this string: the
    // "font changed" test below must compare against what is actually bound,
    // not against nothing. Starting from nullptr made every string flush the
    // batch on its first glyph, which turned one HUD frame into dozens of
    // tiny SDF submissions — and on Android each of those re-specified a
    // buffer the GPU was still reading, so the Adreno driver allocated a new
    // store and memmoved it per string (150 ms a frame in the world HUD).
    const SdfFont* activeFont = sdfFlushSuspended_ ? sdfBatchFont_ : nullptr;

    // ONE scale for the whole string, taken from the metric face. Scaling each
    // glyph by its own face made a line mixed from two faces (a Chinese label
    // whose missing simplifications come from the fallback) render at two
    // sizes, which reads as broken glyphs at small sizes.
    const SdfFont* lineMetricFace = metricsFont();
    const float lineScale = lineMetricFace != nullptr
        ? uiScaleToSdfScale(scale, *lineMetricFace)
        : scale;
    const float lineMinScale = lineMetricFace != nullptr
        ? uiScaleToSdfScale(kMinimumReadableScale, *lineMetricFace)
        : kMinimumReadableScale;

    for (const char* current = text; *current != '\0'; ) {
        std::uint32_t codepoint = 0;
        if (!decodeUtf8(current, codepoint)) {
            break;
        }
        if (codepoint == '\n' || codepoint == '\r') {
            continue;
        }

        const SdfFont& font = fontForCodepoint(codepoint);
        if (&font != activeFont) {
            flushSdfTextBatch();
            activeFont = &font;
            sdfBatchFont_ = activeFont;
            beginSdfTextBatch(framebufferAspect);
        }

        const float sdfScale = lineScale;
        sdfBatchScale_ = std::max(lineScale, lineMinScale);

        const SdfGlyph& glyph = font.glyph(codepoint);
        if (!glyph.valid) {
            continue;
        }

        if (glyph.width > 0.0f && glyph.height > 0.0f) {
            const float width = glyph.width * sdfScale;
            const float height = glyph.height * sdfScale;
            const float centerX = cursorX + (glyph.xoff + glyph.width * 0.5f) * sdfScale;
            const float centerY = baselineY - (glyph.yoff + glyph.height * 0.5f) * sdfScale;
            enqueueSdfGlyph(centerX, centerY, width * 0.5f, height * 0.5f, glyph.uv, red, green, blue, alpha);
        }

        if (glyph.advance > 0.0f) {
            cursorX += glyph.advance * sdfScale * font.letterSpacing();
        }
    }

    // Inside a beginTextBatch()/endTextBatch() scope the caller owns the
    // flush: skip it here so consecutive strings share one SDF submission
    // (font switches still flush mid-string via the loop above).
    if (!sdfFlushSuspended_) {
        flushSdfTextBatch();
    }

    if (restoreRectBatch) {
        beginRectBatch(framebufferAspect);
    }
}

void Renderer2D::drawTextAligned(
    const std::string& text,
    float anchorX,
    float y,
    float scale,
    TextAlign align,
    float framebufferAspect,
    float red,
    float green,
    float blue) {
    const TextMetrics metrics = measureText(text, scale);
    float x = anchorX;
    if (align == TextAlign::Center) {
        x -= metrics.width * 0.5f;
    } else if (align == TextAlign::Right) {
        x -= metrics.width;
    }
    drawText(text.c_str(), x, y, scale, framebufferAspect, red, green, blue);
}

void Renderer2D::drawTextInWidth(
    const std::string& text,
    float anchorX,
    float y,
    float preferredScale,
    float maxWidth,
    TextAlign align,
    float framebufferAspect,
    float red,
    float green,
    float blue) {
    drawTextAligned(text, anchorX, y, fitTextScale(text, preferredScale, maxWidth), align, framebufferAspect, red, green, blue);
}

TextMetrics Renderer2D::measureText(const std::string& text, float scale) const {
    if (!hasAnySdfFont() || text.empty()) {
        return {};
    }

    float width = 0.0f;
    const char* cursor = text.c_str();
    while (*cursor != '\0') {
        std::uint32_t codepoint = 0;
        if (!decodeUtf8(cursor, codepoint)) {
            break;
        }
        if (codepoint == '\n' || codepoint == '\r') {
            continue;
        }

        const SdfFont& font = fontForCodepoint(codepoint);
        if (!font.valid()) {
            continue;
        }

        // Same single scale as drawText(), or the measured width and the drawn
        // extent disagree and centred text clips its last glyph.
        const SdfFont* metricFace = metricsFont();
        const float sdfScale = metricFace != nullptr ? uiScaleToSdfScale(scale, *metricFace) : scale;
        const SdfGlyph& glyph = font.glyph(codepoint);
        if (!glyph.valid || glyph.advance <= 0.0f) {
            continue;
        }
        width += glyph.advance * sdfScale * font.letterSpacing();
    }

    return {
        width,
        SdfFont::kLegacyLineHeight * scale
    };
}

float Renderer2D::estimateTextWidth(const std::string& text, float uiScale) const {
    if (hasAnySdfFont() && !text.empty()) {
        return measureText(text, uiScale).width;
    }

    return SdfFont::estimatedUiTextWidth(text, uiScale);
}

float Renderer2D::fitTextScale(const std::string& text, float preferredScale, float maxWidth) const {
    if (text.empty() || maxWidth <= 0.0f) {
        return preferredScale;
    }

    const float preferredWidth = measureText(text, preferredScale).width;
    if (preferredWidth <= maxWidth) {
        return preferredScale;
    }

    return std::max(kMinimumReadableScale, preferredScale * (maxWidth / preferredWidth));
}

void Renderer2D::initializeSquareMesh() {
    if (!hasDeviceResources()) {
        return;
    }

    const float vertices[] = {
        -1.0f, -1.0f, 0.0f,
         1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,

        -1.0f, -1.0f, 0.0f,
         1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f
    };

    glBindVertexArray(squareVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, squareVbo_->id());
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer2D::initializeTexturedMesh() {
    if (!hasDeviceResources()) {
        return;
    }

    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 0.0f,

        -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
    };

    glBindVertexArray(texturedVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, texturedVbo_->id());
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer2D::initializeBatchMesh() {
    if (!hasDeviceResources()) {
        return;
    }

    glBindVertexArray(batchVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, batchVbo_->id());
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kMaxBatchVertices * sizeof(BatchVertex)),
        nullptr,
        GL_DYNAMIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(BatchVertex));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, u)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, localX)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, blendR)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, blendLeft)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, matKind)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, nextU)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, detailScale)));
    glEnableVertexAttribArray(8);
    // How much of the sprite is under casters. Only the per-part material
    // response reads it; every other kind ignores it, so it is one float for
    // the whole batch rather than three more packed into a slot that already
    // carries three things.
    glVertexAttribPointer(10, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, occluded)));
    glEnableVertexAttribArray(10);
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, materialFlags)));
    glEnableVertexAttribArray(9);
    // Building texture array layer index (attribute 11).
    glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, buildingArrayLayer)));
    glEnableVertexAttribArray(11);
    glVertexAttribPointer(12, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(BatchVertex, transitionMode)));
    glEnableVertexAttribArray(12);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void Renderer2D::initializePolygonShadowMesh() {
    if (!hasDeviceResources()) {
        return;
    }

    glBindVertexArray(polygonShadowVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, polygonShadowVbo_->id());
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kMaxBatchVertices * sizeof(PolygonShadowVertex)),
        nullptr,
        GL_DYNAMIC_DRAW);

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 1, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, alpha)));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Contact pass: a second small dynamic buffer. It must be separate from
    // the sun buffer because the contact pass runs after the blur while the
    // sun set's triangulation cache must survive every flush of the frame.
    glBindVertexArray(contactShadowVao_->id());
    glBindBuffer(GL_ARRAY_BUFFER, contactShadowVbo_->id());
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(
            kMaxContactShadowVertices * sizeof(PolygonShadowVertex)),
        nullptr,
        GL_DYNAMIC_DRAW);

    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, x)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 1, GL_FLOAT, GL_FALSE, sizeof(PolygonShadowVertex),
        reinterpret_cast<void*>(offsetof(PolygonShadowVertex, alpha)));
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

} // namespace uniscope::d2







