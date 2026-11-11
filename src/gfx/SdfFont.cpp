/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "SdfFont.h"

#include "AndroidAssetReader.h"
#include "FontAssets.h"
#include "PlatformLog.h"

#include "uniscope/d2/AssetPaths.h"
#include "Utf8.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uniscope::d2 {

std::vector<uint32_t> collectCodepoints(const std::vector<std::string>& texts) {
    std::vector<uint32_t> codepoints;
    std::unordered_set<uint32_t> seen;
    for (const std::string& text : texts) {
        const char* cursor = text.c_str();
        uint32_t codepoint = 0;
        while (*cursor != '\0') {
            if (!decodeUtf8(cursor, codepoint)) {
                break;
            }
            if (seen.insert(codepoint).second) {
                codepoints.push_back(codepoint);
            }
        }
    }
    std::sort(codepoints.begin(), codepoints.end());
    return codepoints;
}

namespace {

constexpr int kAtlasSize = 2048;
constexpr int kSdfPadding = 8;
constexpr unsigned char kOnEdgeValue = 180;
constexpr float kPixelDistScale = static_cast<float>(kOnEdgeValue) / static_cast<float>(kSdfPadding);
constexpr int kMaxSourceHanCjkGlyphs = 1200;

const SdfGlyph kEmptyGlyph{};

bool readFileBytes(const std::string& path, std::vector<unsigned char>& bytes) {
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (input) {
        input.seekg(0, std::ios::end);
        const std::streamoff size = input.tellg();
        if (size > 0) {
            input.seekg(0, std::ios::beg);
            bytes.resize(static_cast<std::size_t>(size));
            if (input.read(reinterpret_cast<char*>(bytes.data()), size)) {
                return true;
            }
            bytes.clear();
        }
    }

#if defined(__ANDROID__)
    bytes = AndroidAssetReadBytes(path);
    return !bytes.empty();
#else
    return false;
#endif
}

void appendCodepointRange(std::vector<int>& codepoints, int first, int last) {
    for (int codepoint = first; codepoint <= last; ++codepoint) {
        codepoints.push_back(codepoint);
    }
}

void appendIfGlyphExists(stbtt_fontinfo& font, int codepoint, std::vector<int>& codepoints) {
    if (stbtt_FindGlyphIndex(&font, codepoint) != 0) {
        codepoints.push_back(codepoint);
    }
}

void appendSourceHanCodepoints(stbtt_fontinfo& font, std::vector<int>& codepoints) {
    appendCodepointRange(codepoints, 32, 126);
    appendCodepointRange(codepoints, 160, 255);

    static const int kPunctuationRanges[][2] = {
        {0x3000, 0x303F},
        {0xFF01, 0xFF65},
    };
    for (const auto& range : kPunctuationRanges) {
        for (int codepoint = range[0]; codepoint <= range[1]; ++codepoint) {
            appendIfGlyphExists(font, codepoint, codepoints);
        }
    }

    int cjkAdded = 0;
    for (int codepoint = 0x4E00; codepoint <= 0x9FFF && cjkAdded < kMaxSourceHanCjkGlyphs; ++codepoint) {
        if (stbtt_FindGlyphIndex(&font, codepoint) == 0) {
            continue;
        }
        codepoints.push_back(codepoint);
        ++cjkAdded;
    }
}

// The codepoint set a content asks for: the Latin essentials (the UI needs
// them, they pack cheaply, and a font without them could not draw a single
// label), the CJK punctuation any Chinese text needs to look right, and then
// exactly the characters the content itself uses.
//
// The last group is the whole point: a fixed slice of the CJK block is a
// guess about which characters a game might use, and it slices by codepoint
// rather than by use. Asking the content is both cheaper (a few hundred
// glyphs instead of a thousand) and correct.
void appendContentCodepoints(
    stbtt_fontinfo& font,
    const std::vector<std::string>& texts,
    std::vector<int>& codepoints) {
    appendCodepointRange(codepoints, 32, 126);
    appendCodepointRange(codepoints, 160, 255);

    static const int kPunctuationRanges[][2] = {
        {0x3000, 0x303F},
        {0xFF01, 0xFF65},
    };
    for (const auto& range : kPunctuationRanges) {
        for (int codepoint = range[0]; codepoint <= range[1]; ++codepoint) {
            appendIfGlyphExists(font, codepoint, codepoints);
        }
    }

    for (uint32_t codepoint : collectCodepoints(texts)) {
        if (codepoint < 0x0300) {
            continue; // already covered by the Latin essentials
        }
        if (stbtt_FindGlyphIndex(&font, static_cast<int>(codepoint)) == 0) {
            continue; // the font has no such glyph at all
        }
        codepoints.push_back(static_cast<int>(codepoint));
    }
}

void ensureMetricsOnlyGlyph(
    stbtt_fontinfo& font,
    float bakeScale,
    int codepoint,
    std::unordered_map<uint32_t, SdfGlyph>& glyphs) {
    SdfGlyph& stored = glyphs[static_cast<uint32_t>(codepoint)];
    if (stored.valid) {
        return;
    }

    int advance = 0;
    int leftSideBearing = 0;
    stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &leftSideBearing);
    if (advance <= 0) {
        return;
    }

    stored.valid = true;
    stored.advance = static_cast<float>(advance) * bakeScale;
}

// The atlas is one channel: a signed distance is a scalar, and the text shader
// reads `.r`. Padding it to RGBA, as it used to be, quadrupled a 2048^2 upload
// (16 MB instead of 4 MB) that is repeated for every content-driven CJK bake.
void writeAtlasPixel(std::vector<unsigned char>& distancePixels, int atlasX, int atlasY, unsigned char value) {
    const int glY = kAtlasSize - 1 - atlasY;
    const std::size_t index = static_cast<std::size_t>(glY) * static_cast<std::size_t>(kAtlasSize)
        + static_cast<std::size_t>(atlasX);
    distancePixels[index] = value;
}

} // namespace

float SdfFont::estimatedLineHeight(float scale) {
    return kLegacyLineHeight * scale;
}

float SdfFont::estimatedUiTextWidth(const std::string& text, float uiScale) {
    if (text.empty() || uiScale <= 0.0f) {
        return 0.0f;
    }

    return static_cast<float>(text.size()) * kLegacyGlyphWidth * uiScale * kLetterSpacing;
}

float SdfFont::estimatedUiAscent(float uiScale) {
    return kLegacyLineHeight * uiScale * kLegacyAscentRatio;
}

bool SdfFont::loadFontBytes(
    FontFamily family,
    const std::string& path,
    std::vector<unsigned char>& bytes,
    std::string& loadedPath) const {
    if (!path.empty()) {
        if (readFileBytes(path, bytes)) {
            loadedPath = path;
            return true;
        }
        return false;
    }
    for (const std::string& candidate : FontAssets::candidatePaths(family)) {
        if (readFileBytes(candidate, bytes)) {
            loadedPath = candidate;
            return true;
        }
    }
    return false;
}

bool SdfFont::load(FontFamily family) {
    return loadInternal(
        family,
        std::string("family:") + std::to_string(static_cast<int>(family)),
        std::string(),
        nullptr,
        nullptr);
}

bool SdfFont::loadForCodepoints(
    FontFamily family,
    const std::vector<std::string>& codepointTexts,
    SdfFontLoadReport* report) {
    return loadInternal(
        family,
        std::string("family:") + std::to_string(static_cast<int>(family)),
        std::string(),
        &codepointTexts,
        report);
}

bool SdfFont::loadFromPath(
    const std::string& id,
    const std::string& path,
    bool cjk,
    const std::vector<std::string>* codepointTexts,
    SdfFontLoadReport* report) {
    return loadInternal(
        cjk ? FontFamily::SourceHanSans : FontFamily::Default,
        id,
        path,
        codepointTexts,
        report);
}

bool SdfFont::loadInternal(
    FontFamily family,
    const std::string& id,
    const std::string& path,
    const std::vector<std::string>* codepointTexts,
    SdfFontLoadReport* report) {
    valid_ = false;
    monospace_ = false;
    family_ = family;
    id_ = id;
    atlas_ = Texture2D{};
    glyphs_.clear();
    fallbackGlyph_ = SdfGlyph{};

    std::vector<unsigned char> fontBytes;
    std::string loadedPath;
    if (!loadFontBytes(family, path, fontBytes, loadedPath)) {
        UNISCOPE_D2_LOG_ERROR("SdfFont: font '%s' was not found at '%s'\n", id.c_str(), path.c_str());
        return false;
    }

    std::vector<uint32_t> missing;
    if (!buildAtlas(fontBytes, family, codepointTexts, &missing)) {
        UNISCOPE_D2_LOG_ERROR("SdfFont: failed to build SDF atlas from %s\n", loadedPath.c_str());
        return false;
    }

    valid_ = atlas_.valid();
    if (valid_) {
        UNISCOPE_D2_LOG_INFO(
            "SdfFont: loaded %s face=%s glyphs=%zu (%dx%d atlas)%s\n",
            loadedPath.c_str(),
            id_.c_str(),
            glyphs_.size(),
            atlas_.width(),
            atlas_.height(),
            codepointTexts != nullptr ? " [content-driven]" : "");
        if (report != nullptr) {
            report->missing = missing;
            report->baked = static_cast<int>(glyphs_.size());
            report->requested = report->baked + static_cast<int>(missing.size());
        }
        if (!missing.empty()) {
            // A codepoint that did not fit renders as a blank, so say so.
            std::string line = "SdfFont: " + std::to_string(missing.size())
                + " requested glyph(s) did not fit the " + std::to_string(atlas_.width())
                + "x" + std::to_string(atlas_.height()) + " atlas:";
            char codepoint[16];
            for (std::size_t index = 0; index < missing.size() && index < 32; ++index) {
                std::snprintf(codepoint, sizeof(codepoint), " U+%04X", missing[index]);
                line += codepoint;
            }
            UNISCOPE_D2_LOG_ERROR("%s\n", line.c_str());
        }
    }
    return valid_;
}

bool SdfFont::buildAtlas(
    const std::vector<unsigned char>& fontBytes,
    FontFamily family,
    const std::vector<std::string>* codepointTexts,
    std::vector<uint32_t>* missingCodepoints) {
    if (missingCodepoints != nullptr) {
        missingCodepoints->clear();
    }
    stbtt_fontinfo font{};
    if (stbtt_InitFont(&font, fontBytes.data(), stbtt_GetFontOffsetForIndex(fontBytes.data(), 0)) == 0) {
        return false;
    }

    const float bakeScale = stbtt_ScaleForPixelHeight(&font, kBakePixelHeight);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    ascentBake_ = static_cast<float>(ascent) * bakeScale;
    lineHeightBake_ = static_cast<float>(ascent - descent + lineGap) * bakeScale;
    sdfEdge_ = static_cast<float>(kOnEdgeValue) / 255.0f;

    std::vector<int> codepoints;
    if (codepointTexts != nullptr) {
        codepoints.reserve(400);
        appendContentCodepoints(font, *codepointTexts, codepoints);
    }
    else if (family == FontFamily::SourceHanSans) {
        codepoints.reserve(1600);
        appendSourceHanCodepoints(font, codepoints);
    } else {
        codepoints.reserve(224);
        appendCodepointRange(codepoints, 32, 126);
        appendCodepointRange(codepoints, 160, 255);
    }

    struct PendingGlyph {
        int codepoint = 0;
        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        float advance = 0.0f;
        unsigned char* bitmap = nullptr;
    };

    std::vector<PendingGlyph> pending;
    pending.reserve(codepoints.size());
    for (int codepoint : codepoints) {
        int width = 0;
        int height = 0;
        int xoff = 0;
        int yoff = 0;
        unsigned char* bitmap = stbtt_GetCodepointSDF(
            &font,
            bakeScale,
            codepoint,
            kSdfPadding,
            kOnEdgeValue,
            kPixelDistScale,
            &width,
            &height,
            &xoff,
            &yoff);
        if (bitmap == nullptr || width <= 0 || height <= 0) {
            stbtt_FreeSDF(bitmap, nullptr);
            if (codepoint == ' ' || codepoint == '\t') {
                ensureMetricsOnlyGlyph(font, bakeScale, codepoint, glyphs_);
            }
            continue;
        }

        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &leftSideBearing);

        PendingGlyph glyph;
        glyph.codepoint = codepoint;
        glyph.width = width;
        glyph.height = height;
        glyph.xoff = xoff;
        glyph.yoff = yoff;
        glyph.advance = static_cast<float>(advance) * bakeScale;
        glyph.bitmap = bitmap;
        pending.push_back(glyph);
    }

    if (pending.empty()) {
        return false;
    }

    std::vector<PendingGlyph> essential;
    std::vector<PendingGlyph> extended;
    essential.reserve(pending.size());
    extended.reserve(pending.size());
    for (PendingGlyph& glyph : pending) {
        if (glyph.codepoint < 0x0300) {
            essential.push_back(glyph);
        } else {
            extended.push_back(glyph);
        }
    }

    const auto sortByAreaDescending = [](const PendingGlyph& a, const PendingGlyph& b) {
        const int areaA = a.width * a.height;
        const int areaB = b.width * b.height;
        if (areaA != areaB) {
            return areaA > areaB;
        }
        return a.codepoint < b.codepoint;
    };
    std::sort(essential.begin(), essential.end(), sortByAreaDescending);
    std::sort(extended.begin(), extended.end(), sortByAreaDescending);

    pending.clear();
    pending.insert(pending.end(), essential.begin(), essential.end());
    pending.insert(pending.end(), extended.begin(), extended.end());

    std::vector<stbrp_rect> rects(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        rects[index].w = pending[index].width;
        rects[index].h = pending[index].height;
        rects[index].id = static_cast<int>(index);
    }

    stbrp_context packContext{};
    std::vector<unsigned char> packNodes(static_cast<std::size_t>(kAtlasSize) * 2);
    stbrp_init_target(&packContext, kAtlasSize, kAtlasSize, reinterpret_cast<stbrp_node*>(packNodes.data()), static_cast<int>(packNodes.size()));
    stbrp_pack_rects(&packContext, rects.data(), static_cast<int>(rects.size()));

    const bool requireFullPack = family == FontFamily::Default;
    int packedEssential = 0;
    int packedExtended = 0;
    for (std::size_t index = 0; index < rects.size(); ++index) {
        if (rects[index].was_packed == 0) {
            continue;
        }
        if (pending[index].codepoint < 0x0300) {
            ++packedEssential;
        } else {
            ++packedExtended;
        }
    }

    const int essentialCount = static_cast<int>(essential.size());
    if (packedEssential == 0) {
        for (PendingGlyph& glyph : pending) {
            stbtt_FreeSDF(glyph.bitmap, nullptr);
        }
        return false;
    }

    if (requireFullPack && (packedEssential + packedExtended) != static_cast<int>(pending.size())) {
        for (PendingGlyph& glyph : pending) {
            stbtt_FreeSDF(glyph.bitmap, nullptr);
        }
        return false;
    }

    if (!requireFullPack && packedEssential < essentialCount) {
        std::fprintf(
            stderr,
            "SdfFont: failed to pack essential glyphs (%d/%d)\n",
            packedEssential,
            essentialCount);
        for (PendingGlyph& glyph : pending) {
            stbtt_FreeSDF(glyph.bitmap, nullptr);
        }
        return false;
    }

    if (!requireFullPack && (packedEssential + packedExtended) < static_cast<int>(pending.size())) {
        std::fprintf(
            stderr,
            "SdfFont: packed %d essential + %d extended/%zu glyphs into atlas (increase atlas size or reduce CJK range to fit more)\n",
            packedEssential,
            packedExtended,
            pending.size());
    }

    std::vector<unsigned char> distancePixels(static_cast<std::size_t>(kAtlasSize) * static_cast<std::size_t>(kAtlasSize), 0);
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const stbrp_rect& rect = rects[index];
        PendingGlyph& glyph = pending[index];
        if (rect.was_packed == 0) {
            stbtt_FreeSDF(glyph.bitmap, nullptr);
            glyph.bitmap = nullptr;
            // The Latin essentials cannot be missing: the load fails without
            // them. The extended set can, and a codepoint that did not fit
            // renders as a blank, so it is reported to the caller.
            if (missingCodepoints != nullptr && glyph.codepoint >= 0x0300) {
                missingCodepoints->push_back(static_cast<uint32_t>(glyph.codepoint));
            }
            continue;
        }

        for (int y = 0; y < glyph.height; ++y) {
            for (int x = 0; x < glyph.width; ++x) {
                const std::size_t srcOffset = static_cast<std::size_t>(y) * static_cast<std::size_t>(glyph.width) + static_cast<std::size_t>(x);
                writeAtlasPixel(distancePixels, rect.x + x, rect.y + y, glyph.bitmap[srcOffset]);
            }
        }

        SdfGlyph& stored = glyphs_[static_cast<uint32_t>(glyph.codepoint)];
        stored.valid = true;
        stored.xoff = static_cast<float>(glyph.xoff);
        stored.yoff = static_cast<float>(glyph.yoff);
        stored.width = static_cast<float>(glyph.width);
        stored.height = static_cast<float>(glyph.height);
        stored.advance = glyph.advance;
        stored.uv.u0 = static_cast<float>(rect.x) / static_cast<float>(kAtlasSize);
        stored.uv.u1 = static_cast<float>(rect.x + glyph.width) / static_cast<float>(kAtlasSize);
        stored.uv.v0 = 1.0f - static_cast<float>(rect.y) / static_cast<float>(kAtlasSize);
        stored.uv.v1 = 1.0f - static_cast<float>(rect.y + glyph.height) / static_cast<float>(kAtlasSize);

        stbtt_FreeSDF(glyph.bitmap, nullptr);
        glyph.bitmap = nullptr;
    }

    ensureMetricsOnlyGlyph(font, bakeScale, ' ', glyphs_);
    ensureMetricsOnlyGlyph(font, bakeScale, '\t', glyphs_);

    const auto spaceIt = glyphs_.find(static_cast<uint32_t>(' '));
    const auto anchorIt = glyphs_.find(static_cast<uint32_t>('A'));
    if (spaceIt != glyphs_.end() && anchorIt != glyphs_.end() && spaceIt->second.advance > 0.0f) {
        const float advanceDelta = std::abs(spaceIt->second.advance - anchorIt->second.advance);
        monospace_ = advanceDelta < kBakePixelHeight * 0.05f;
    } else {
        monospace_ = false;
    }

    fallbackGlyph_ = spaceIt != glyphs_.end() ? spaceIt->second : SdfGlyph{};
    if (fallbackGlyph_.valid) {
        for (auto& entry : glyphs_) {
            SdfGlyph& glyph = entry.second;
            if (glyph.valid) {
                continue;
            }
            glyph.valid = true;
            glyph.advance = fallbackGlyph_.advance > 0.0f
                ? fallbackGlyph_.advance
                : kBakePixelHeight * 0.35f;
        }
    }

    atlas_ = Texture2D::loadFromPixels(distancePixels, kAtlasSize, kAtlasSize, uniscope::d2::TextureQuality::High, 1);
    return atlas_.valid();
}

const SdfGlyph& SdfFont::glyph(uint32_t codepoint) const {
    const auto found = glyphs_.find(codepoint);
    if (found != glyphs_.end()) {
        return found->second;
    }
    return fallbackGlyph_.valid ? fallbackGlyph_ : kEmptyGlyph;
}

bool SdfFont::hasGlyph(uint32_t codepoint) const {
    // The map holds exactly the glyphs that made it into the atlas (plus the
    // metrics-only space), so membership is the answer.
    return glyphs_.find(codepoint) != glyphs_.end();
}

float SdfFont::measureString(const std::string& text, float scale) const {
    if (!valid_ || text.empty()) {
        return 0.0f;
    }

    float width = 0.0f;
    const char* cursor = text.c_str();
    while (*cursor != '\0') {
        uint32_t codepoint = 0;
        if (!decodeUtf8(cursor, codepoint)) {
            break;
        }
        if (codepoint == '\n' || codepoint == '\r') {
            continue;
        }

        const SdfGlyph& info = glyph(codepoint);
        if (!info.valid) {
            continue;
        }
        width += info.advance * scale * letterSpacing();
    }
    return width;
}

} // namespace uniscope::d2
