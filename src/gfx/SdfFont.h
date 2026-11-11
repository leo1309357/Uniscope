/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_SDF_FONT_H
#define UNISCOPE_D2_GFX_SDF_FONT_H

#include "FontAssets.h"
#include "FontFamily.h"
#include "Texture2D.h"
#include "UvRect.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace uniscope::d2 {

struct SdfGlyph {
    UvRect uv{};
    float xoff = 0.0f;
    float yoff = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float advance = 0.0f;
    bool valid = false;
};

// What a bake asked for and what it got.
//
// The missing list is the point of the whole exercise. A codepoint that was
// asked for and did not fit the atlas renders as a blank, and a content
// author needs to hear that in a log rather than see a blank glyph on screen.
struct SdfFontLoadReport {
    int requested = 0;
    int baked = 0;
    std::vector<uint32_t> missing;
};

class SdfFont {
public:
    static constexpr float kLetterSpacing = 1.25f;
    static constexpr float kLegacyLineHeight = 7.0f;
    static constexpr float kLegacyGlyphWidth = 5.0f;
    static constexpr float kLegacyAscentRatio = 6.0f / 7.0f;
    static constexpr float kBakePixelHeight = 48.0f;

    static float estimatedLineHeight(float scale);
    static float estimatedUiTextWidth(const std::string& text, float uiScale);
    static float estimatedUiAscent(float uiScale);

    bool load(FontFamily family = FontAssets::primaryUiFamily());

    // Bake a font for exactly the codepoints the content needs.
    //
    // The Latin essentials always come along (the UI needs them, they pack
    // cheaply, and a font without them could not draw a single label); the
    // extra codepoints are the content's own.
    //
    // This is what replaces the fixed CJK slice. A slice is a guess about
    // which Chinese characters a game might use, and it slices by CODEPOINT,
    // not by use - so it covered 19% of the characters the game's own
    // dialogue is written in. The content knows its own characters; asking
    // it is both cheaper and correct.
    bool loadForCodepoints(
        FontFamily family,
        const std::vector<std::string>& codepointTexts,
        SdfFontLoadReport* report = nullptr);

    // Load one face named by the font manifest.
    //
    // `path` is already resolved (see resolveFontPath); `cjk` picks the CJK
    // default slice when no content is given. This is the entry point the
    // multi-face baker uses: the family enum only survives for the two
    // legacy call sites that still ask by role.
    bool loadFromPath(
        const std::string& id,
        const std::string& path,
        bool cjk,
        const std::vector<std::string>* codepointTexts,
        SdfFontLoadReport* report = nullptr);
    bool loadFromPath(
        const std::string& id,
        const std::string& path,
        bool cjk,
        const std::vector<std::string>& codepointTexts,
        SdfFontLoadReport* report = nullptr) {
        return loadFromPath(id, path, cjk, &codepointTexts, report);
    }

    // Whether the codepoint is baked. A codepoint that is not baked renders
    // as the fallback (a blank), so this is the question a content pipeline
    // asks before it trusts its text.
    bool hasGlyph(uint32_t codepoint) const;

    bool valid() const { return valid_; }
    FontFamily family() const { return family_; }
    // The manifest id ("zh-body", "en-display"), or "family:<n>" for a face
    // loaded by Family.
    const std::string& id() const { return id_; }

    const Texture2D& texture() const { return atlas_; }

    float ascentBake() const { return ascentBake_; }
    float lineHeightBake() const { return lineHeightBake_; }
    float sdfEdge() const { return sdfEdge_; }
    float letterSpacing() const { return monospace_ ? 1.0f : kLetterSpacing; }

    const SdfGlyph& glyph(uint32_t codepoint) const;
    float measureString(const std::string& text, float scale) const;

private:
    bool loadFontBytes(
        FontFamily family,
        const std::string& path,
        std::vector<unsigned char>& bytes,
        std::string& loadedPath) const;
    bool loadInternal(
        FontFamily family,
        const std::string& id,
        const std::string& path,
        const std::vector<std::string>* codepointTexts,
        SdfFontLoadReport* report);
    bool buildAtlas(
        const std::vector<unsigned char>& fontBytes,
        FontFamily family,
        const std::vector<std::string>* codepointTexts,
        std::vector<uint32_t>* missingCodepoints);

    Texture2D atlas_;
    std::unordered_map<uint32_t, SdfGlyph> glyphs_;
    SdfGlyph fallbackGlyph_{};
    float ascentBake_ = 40.0f;
    float lineHeightBake_ = kBakePixelHeight;
    float sdfEdge_ = 180.0f / 255.0f;
    bool monospace_ = false;
    bool valid_ = false;
    FontFamily family_ = FontFamily::Default;
    std::string id_;
};

// The distinct codepoints a set of UTF-8 strings needs, sorted.
//
// This is the other half of the content-driven atlas: the content declares
// its characters, the font is baked for exactly those, and nothing else is
// paid for.
std::vector<uint32_t> collectCodepoints(const std::vector<std::string>& texts);

} // namespace uniscope::d2

#endif
