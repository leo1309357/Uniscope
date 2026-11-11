/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_UTF8_H
#define UNISCOPE_D2_GFX_UTF8_H

#include <cstdint>
#include <string>

namespace uniscope::d2 {

inline bool decodeUtf8(const char*& cursor, std::uint32_t& codepoint) {
    const unsigned char byte0 = static_cast<unsigned char>(*cursor);
    if (byte0 == 0) {
        return false;
    }

    if (byte0 < 0x80) {
        codepoint = byte0;
        ++cursor;
        return true;
    }

    if ((byte0 & 0xE0) == 0xC0) {
        const unsigned char byte1 = static_cast<unsigned char>(cursor[1]);
        if ((byte1 & 0xC0) != 0x80) {
            codepoint = byte0;
            ++cursor;
            return true;
        }
        codepoint = ((byte0 & 0x1F) << 6) | (byte1 & 0x3F);
        cursor += 2;
        return true;
    }

    if ((byte0 & 0xF0) == 0xE0) {
        const unsigned char byte1 = static_cast<unsigned char>(cursor[1]);
        const unsigned char byte2 = static_cast<unsigned char>(cursor[2]);
        if ((byte1 & 0xC0) != 0x80 || (byte2 & 0xC0) != 0x80) {
            codepoint = byte0;
            ++cursor;
            return true;
        }
        codepoint = ((byte0 & 0x0F) << 12) | ((byte1 & 0x3F) << 6) | (byte2 & 0x3F);
        cursor += 3;
        return true;
    }

    if ((byte0 & 0xF8) == 0xF0) {
        const unsigned char byte1 = static_cast<unsigned char>(cursor[1]);
        const unsigned char byte2 = static_cast<unsigned char>(cursor[2]);
        const unsigned char byte3 = static_cast<unsigned char>(cursor[3]);
        if ((byte1 & 0xC0) != 0x80 || (byte2 & 0xC0) != 0x80 || (byte3 & 0xC0) != 0x80) {
            codepoint = byte0;
            ++cursor;
            return true;
        }
        codepoint = ((byte0 & 0x07) << 18) | ((byte1 & 0x3F) << 12) | ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
        cursor += 4;
        return true;
    }

    codepoint = byte0;
    ++cursor;
    return true;
}

// The UTF-8 spelling of one codepoint. Single-character strings are how the
// baker hands a fallback face just the characters the earlier faces missed:
// SdfFont takes text, and one codepoint per string is exact.
inline std::string encodeUtf8(std::uint32_t codepoint) {
    std::string out;
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    return out;
}

} // namespace uniscope::d2

#endif
