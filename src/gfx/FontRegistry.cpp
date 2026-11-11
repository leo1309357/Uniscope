/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "FontRegistry.h"

#include "FontAssets.h"
#include "PlatformLog.h"

#include "uniscope/d2/AssetPaths.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace uniscope::d2 {

namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (char character : text) {
        lowered.push_back(
            (character >= 'A' && character <= 'Z') ? static_cast<char>(character - 'A' + 'a')
                                                   : character);
    }
    return lowered;
}

std::vector<std::string_view> splitTabs(std::string_view line) {
    std::vector<std::string_view> cells;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= line.size(); ++index) {
        if (index == line.size() || line[index] == '\t') {
            cells.push_back(line.substr(start, index - start));
            start = index + 1;
        }
    }
    return cells;
}

bool isBlankOrComment(std::string_view line) {
    std::size_t index = 0;
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) {
        ++index;
    }
    return index == line.size() || line[index] == '#';
}

bool isIdShaped(const std::string& id) {
    if (id.empty()) {
        return false;
    }
    for (char character : id) {
        const bool digit = character >= '0' && character <= '9';
        const bool lower = character >= 'a' && character <= 'z';
        const bool upper = character >= 'A' && character <= 'Z';
        if (!digit && !lower && !upper && character != '.' && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

} // namespace

const char* FontRegistry::scriptId(FontScript script) {
    switch (script) {
    case FontScript::Latin: return "latin";
    case FontScript::Chinese: return "chinese";
    case FontScript::Japanese: return "japanese";
    case FontScript::Count: break;
    }
    return "latin";
}

const char* FontRegistry::roleId(FontRole role) {
    switch (role) {
    case FontRole::Body: return "body";
    case FontRole::Emphasis: return "emphasis";
    case FontRole::Display: return "display";
    case FontRole::Mono: return "mono";
    case FontRole::Spare: return "spare";
    case FontRole::Count: break;
    }
    return "body";
}

bool FontRegistry::parseScript(std::string_view text, FontScript* out) {
    const std::string key = lower(trim(text));
    for (std::size_t index = 0; index < static_cast<std::size_t>(FontScript::Count); ++index) {
        const FontScript script = static_cast<FontScript>(index);
        if (key == scriptId(script)) {
            if (out != nullptr) {
                *out = script;
            }
            return true;
        }
    }
    // Spellings a manifest author reaches for before reading this header.
    if (key == "en" || key == "english") {
        if (out != nullptr) {
            *out = FontScript::Latin;
        }
        return true;
    }
    if (key == "zh" || key == "chs" || key == "zh-cn" || key == "cn") {
        if (out != nullptr) {
            *out = FontScript::Chinese;
        }
        return true;
    }
    if (key == "ja" || key == "jp" || key == "ja-jp") {
        if (out != nullptr) {
            *out = FontScript::Japanese;
        }
        return true;
    }
    return false;
}

bool FontRegistry::parseRole(std::string_view text, FontRole* out) {
    const std::string key = lower(trim(text));
    for (std::size_t index = 0; index < static_cast<std::size_t>(FontRole::Count); ++index) {
        const FontRole role = static_cast<FontRole>(index);
        if (key == roleId(role)) {
            if (out != nullptr) {
                *out = role;
            }
            return true;
        }
    }
    if (key == "title" || key == "heading") {
        if (out != nullptr) {
            *out = FontRole::Display;
        }
        return true;
    }
    if (key == "bold") {
        if (out != nullptr) {
            *out = FontRole::Emphasis;
        }
        return true;
    }
    return false;
}

FontRegistry& FontRegistry::instance() {
    static FontRegistry registry;
    return registry;
}

bool FontRegistry::load() {
    const std::string path = FontAssets::manifestPath();
    if (path.empty()) {
        problems_.clear();
        faces_.clear();
        problems_.push_back("fonts/fonts.tsv was not found beside the executable or up its tree");
        UNISCOPE_D2_LOG_ERROR("FontRegistry: %s\n", problems_.back().c_str());
        return false;
    }
    return loadFromFile(path);
}

bool FontRegistry::loadFromFile(const std::string& path) {
    // u8path so a manifest under a non-ASCII directory still opens on Windows,
    // where the narrow API reads the ANSI code page.
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file.is_open()) {
        faces_.clear();
        problems_.assign({std::string("cannot open the font manifest '") + path + "'"});
        UNISCOPE_D2_LOG_ERROR("FontRegistry: %s\n", problems_.back().c_str());
        return false;
    }
    std::string contents(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return loadFromText(contents);
}

bool FontRegistry::loadFromText(std::string_view text) {
    faces_.clear();
    problems_.clear();

    if (text.substr(0, 3) == "\xEF\xBB\xBF") {
        text.remove_prefix(3);
    }

    // Column indices, resolved from the header so a manifest can grow a column
    // without every row moving.
    int idColumn = -1;
    int scriptColumn = -1;
    int roleColumn = -1;
    int pathColumn = -1;
    int weightColumn = -1;
    int italicColumn = -1;
    int familyColumn = -1;
    int orderColumn = -1;
    int eagerColumn = -1;
    int noteColumn = -1;
    int columns = 0;
    bool headerSeen = false;
    unsigned lineNumber = 0;

    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t newline = text.find('\n', position);
        const std::size_t lineEnd = newline == std::string_view::npos ? text.size() : newline;
        std::string_view line = text.substr(position, lineEnd - position);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        position = lineEnd == text.size() ? text.size() + 1 : lineEnd + 1;
        ++lineNumber;

        if (isBlankOrComment(line)) {
            continue;
        }

        const std::vector<std::string_view> cells = splitTabs(line);
        columns = static_cast<int>(cells.size());

        if (!headerSeen) {
            for (int index = 0; index < columns; ++index) {
                const std::string name = lower(trim(cells[static_cast<std::size_t>(index)]));
                if (name == "id") {
                    idColumn = index;
                } else if (name == "script") {
                    scriptColumn = index;
                } else if (name == "role") {
                    roleColumn = index;
                } else if (name == "path") {
                    pathColumn = index;
                } else if (name == "weight") {
                    weightColumn = index;
                } else if (name == "italic") {
                    italicColumn = index;
                } else if (name == "family") {
                    familyColumn = index;
                } else if (name == "order") {
                    orderColumn = index;
                } else if (name == "eager") {
                    eagerColumn = index;
                } else if (name == "note") {
                    noteColumn = index;
                }
            }
            if (idColumn < 0 || scriptColumn < 0 || roleColumn < 0 || pathColumn < 0) {
                problems_.push_back(
                    "line " + std::to_string(lineNumber)
                    + ": the header needs id, script, role and path");
                return false;
            }
            headerSeen = true;
            continue;
        }

        auto cell = [&](int column) -> std::string {
            if (column < 0 || column >= columns) {
                return std::string();
            }
            return trim(cells[static_cast<std::size_t>(column)]);
        };

        FontFace face;
        face.id = cell(idColumn);
        if (!isIdShaped(face.id)) {
            problems_.push_back(
                "line " + std::to_string(lineNumber) + ": '" + face.id + "' is not a face id");
            continue;
        }
        FontScript script = FontScript::Count;
        if (!parseScript(cell(scriptColumn), &script)) {
            problems_.push_back(
                "line " + std::to_string(lineNumber) + ": unknown script '" + cell(scriptColumn)
                + "' for face '" + face.id + "'");
            continue;
        }
        face.script = script;
        FontRole role = FontRole::Count;
        if (!parseRole(cell(roleColumn), &role)) {
            problems_.push_back(
                "line " + std::to_string(lineNumber) + ": unknown role '" + cell(roleColumn)
                + "' for face '" + face.id + "'");
            continue;
        }
        face.role = role;
        face.path = cell(pathColumn);
        if (face.path.empty()) {
            problems_.push_back(
                "line " + std::to_string(lineNumber) + ": face '" + face.id + "' has no path");
            continue;
        }
        const std::string weight = cell(weightColumn);
        if (!weight.empty()) {
            try {
                face.weight = std::stoi(weight);
            } catch (...) {
                problems_.push_back(
                    "line " + std::to_string(lineNumber) + ": weight '" + weight + "' is not a number");
            }
        }
        const std::string italic = lower(cell(italicColumn));
        face.italic = italic == "1" || italic == "true" || italic == "yes";
        face.family = cell(familyColumn);
        const std::string order = cell(orderColumn);
        if (!order.empty()) {
            try {
                face.order = std::stoi(order);
            } catch (...) {
                problems_.push_back(
                    "line " + std::to_string(lineNumber) + ": order '" + order + "' is not a number");
            }
        }
        const std::string eager = lower(cell(eagerColumn));
        face.eager = eager == "1" || eager == "true" || eager == "yes";
        face.note = cell(noteColumn);

        const bool duplicate = std::any_of(
            faces_.begin(), faces_.end(), [&](const FontFace& existing) { return existing.id == face.id; });
        if (duplicate) {
            problems_.push_back(
                "line " + std::to_string(lineNumber) + ": face id '" + face.id
                + "' is already defined (the first one wins)");
            continue;
        }
        faces_.push_back(std::move(face));
    }

    if (!headerSeen) {
        problems_.push_back("the font manifest has no header row");
        return false;
    }

    // Stable order for callers that iterate: by script, then role, then the
    // manifest's own fallback rank. The manifest order is already the tie
    // breaker (std::stable_sort keeps it).
    std::stable_sort(
        faces_.begin(),
        faces_.end(),
        [](const FontFace& left, const FontFace& right) {
            if (left.script != right.script) {
                return static_cast<int>(left.script) < static_cast<int>(right.script);
            }
            if (left.role != right.role) {
                return static_cast<int>(left.role) < static_cast<int>(right.role);
            }
            return left.order < right.order;
        });

    for (const std::string& problem : problems_) {
        UNISCOPE_D2_LOG_ERROR("FontRegistry: %s\n", problem.c_str());
    }
    UNISCOPE_D2_LOG_INFO(
        "FontRegistry: %zu face(s) registered, %zu manifest problem(s)\n",
        faces_.size(),
        problems_.size());
    return !faces_.empty();
}

const FontFace* FontRegistry::find(std::string_view id) const {
    for (const FontFace& face : faces_) {
        if (face.id == id) {
            return &face;
        }
    }
    return nullptr;
}

std::vector<const FontFace*> FontRegistry::chain(FontScript script, FontRole role) const {
    std::vector<const FontFace*> matches;
    for (const FontFace& face : faces_) {
        if (face.script == script && face.role == role) {
            matches.push_back(&face);
        }
    }
    if (matches.empty() && role != FontRole::Spare) {
        // A role with no face of its own borrows the spare pool rather than
        // leaving the slot empty: a weight nobody claimed is still a usable
        // body face when the alternative is no text at all.
        for (const FontFace& face : faces_) {
            if (face.script == script && face.role == FontRole::Spare) {
                matches.push_back(&face);
            }
        }
    }
    return matches;
}

const FontFace* FontRegistry::primary(FontScript script, FontRole role) const {
    const std::vector<const FontFace*> faces = chain(script, role);
    return faces.empty() ? nullptr : faces.front();
}

bool resolveFontPath(const FontFace& face, std::string* outPath) {
    if (face.path.empty()) {
        return false;
    }
    const std::vector<std::string> candidates = FontAssets::treeCandidates(face.path);
    if (!candidates.empty()) {
        if (outPath != nullptr) {
            *outPath = candidates.front();
        }
        return true;
    }
    // A path spelled under resources/ may be rooted by AssetPaths (an APK
    // staging area, or a custom root set by the platform layer) rather than by
    // the tree walk above.
    constexpr const char* kResourcesPrefix = "resources/";
    if (face.path.compare(0, std::string(kResourcesPrefix).size(), kResourcesPrefix) == 0) {
        const std::string relative = face.path.substr(std::string(kResourcesPrefix).size());
        if (AssetPaths::exists(relative)) {
            if (outPath != nullptr) {
                *outPath = AssetPaths::resolve(relative);
            }
            return true;
        }
    }
    return false;
}

} // namespace uniscope::d2
