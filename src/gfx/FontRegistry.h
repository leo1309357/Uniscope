/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_FONT_REGISTRY_H
#define UNISCOPE_D2_GFX_FONT_REGISTRY_H

#include <string>
#include <string_view>
#include <vector>

namespace uniscope::d2 {

// Which writing system a face is drawn for. It is NOT the language of the
// label: English UI running in a Chinese session still draws its punctuation,
// digits and Latin words with the Chinese face, because that face carries a
// Latin set designed next to the Han glyphs and mixing two families inside one
// sentence reads worse than either alone.
enum class FontScript {
    Latin = 0,
    Chinese,
    Japanese,
    Count,
};

// What the text is FOR. A role is a slot in the typographic hierarchy, not a
// weight: the registry decides which weight fills it.
enum class FontRole {
    Body = 0,
    Emphasis,
    Display,
    Mono,
    // Registered and loadable, but no role claims it yet. Kept so an extra
    // weight or italic is one manifest row away from being usable.
    Spare,
    Count,
};

// One font file the game may bake.
//
// `path` is repo-relative ("fonts/en_US/.../AlegreyaSans-Regular-13.ttf" or
// "resources/assets/eraphanis/fonts/..."), so the manifest moves with the
// asset tree instead of hard-coding an absolute location.
struct FontFace {
    std::string id;
    FontScript script = FontScript::Latin;
    FontRole role = FontRole::Body;
    int weight = 400;
    bool italic = false;
    std::string family;
    std::string path;
    // Fallback rank inside (script, role): lower bakes first and wins. Faces
    // with the same rank fall back by file order.
    int order = 0;
    // Bake this face at load even though no chain asked for it yet. Off for
    // everything except what the game draws today: a CJK weight costs about
    // 0.7 s and one 2048^2 atlas each, so "load them all" is a budget decision
    // the author makes in the manifest, not a default.
    bool eager = false;
    std::string note;
};

// The font manifest: which files exist, what each one is for, and the order in
// which they cover for each other.
//
// Why a manifest and not a hard-coded list: the bake pipeline used to know
// exactly two families (a Latin face and one CJK face). Every new weight,
// language or display face meant a code edit and a rebuild. A row per file
// makes the whole tree loadable without touching the renderer, and the
// fallback order is data, so "this font is missing these 124 characters, let
// the next one cover them" is a property of the table rather than of a switch.
class FontRegistry {
public:
    static FontRegistry& instance();

    // Loads the shipped manifest: fonts/fonts.tsv beside the executable (or up
    // the tree, and as an APK asset on Android). Returns false and leaves the
    // registry empty when the file is not found or does not parse.
    bool load();
    bool loadFromFile(const std::string& path);
    bool loadFromText(std::string_view text);

    bool empty() const { return faces_.empty(); }
    std::size_t size() const { return faces_.size(); }
    const std::vector<FontFace>& faces() const { return faces_; }

    const FontFace* find(std::string_view id) const;

    // Every face for (script, role), primary first. Falls back to the Spare
    // pool only when the role itself has no face for that script - never the
    // other way round.
    std::vector<const FontFace*> chain(FontScript script, FontRole role) const;

    // The face that owns a (script, role) slot outright, or nullptr.
    const FontFace* primary(FontScript script, FontRole role) const;

    // Problems the last load recorded (unknown scripts, duplicate ids, missing
    // paths). Content errors are reported, never thrown: a manifest with one
    // bad row still loads the other rows.
    const std::vector<std::string>& problems() const { return problems_; }

    static const char* scriptId(FontScript script);
    static const char* roleId(FontRole role);
    static bool parseScript(std::string_view text, FontScript* out);
    static bool parseRole(std::string_view text, FontRole* out);

private:
    std::vector<FontFace> faces_;
    std::vector<std::string> problems_;
};

// An absolute, existing path for a manifest row (APK asset path on Android).
// False when the file is nowhere the runtime looks.
bool resolveFontPath(const FontFace& face, std::string* outPath);

} // namespace uniscope::d2

#endif
