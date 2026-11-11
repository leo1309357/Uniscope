/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_FONT_ASSETS_H
#define UNISCOPE_D2_GFX_FONT_ASSETS_H

#include "FontFamily.h"

#include <string>
#include <vector>

namespace uniscope::d2 {

struct FontVariant {
    const char* id = "";
    const char* relativePath = "";
};

class FontAssets {
public:
    static FontFamily primaryUiFamily();

    static const std::vector<FontVariant>& variants(FontFamily family);
    static std::vector<std::string> candidatePaths(FontFamily family);

    // Every place a repo-relative path can live: beside the executable, up the
    // tree from it, the working directory and its parents, and - on Android -
    // the APK asset store. The font manifest and the fonts it names share this
    // resolution, so one tree moved next to a binary keeps working.
    static std::vector<std::string> treeCandidates(const std::string& relativePath);

    // The shipped font manifest (fonts/fonts.tsv), or "" when it is absent.
    static std::string manifestPath();

private:
    static const std::vector<FontVariant>& defaultVariants();
    static const std::vector<FontVariant>& sourceHanSansVariants();
};

} // namespace uniscope::d2

#endif
