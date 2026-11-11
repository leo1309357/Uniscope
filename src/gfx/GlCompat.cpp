/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "GlCompat.h"

#if defined(__ANDROID__)
#include <cstring>

namespace uniscope::d2 {

namespace {
int gAnisotropyCached = -1; // -1 unknown, 0 no, 1 yes
}

void GlInvalidateExtensionCache() {
    gAnisotropyCached = -1;
}

bool GlHasAnisotropy() {
    if (gAnisotropyCached >= 0) {
        return gAnisotropyCached == 1;
    }
    // glGetString(GL_EXTENSIONS) is removed in core ES 3.0; enumerate with
    // glGetStringi. Any failure (no context) means "not available".
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    if (count <= 0) {
        // Fallback for drivers that still expose the legacy string.
        const char* legacy =
            reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (legacy != nullptr &&
            std::strstr(legacy, "GL_EXT_texture_filter_anisotropic") != nullptr) {
            gAnisotropyCached = 1;
            return true;
        }
        gAnisotropyCached = 0;
        return false;
    }
    for (GLint i = 0; i < count; ++i) {
        const char* ext =
            reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (ext != nullptr &&
            std::strcmp(ext, "GL_EXT_texture_filter_anisotropic") == 0) {
            gAnisotropyCached = 1;
            return true;
        }
    }
    gAnisotropyCached = 0;
    return false;
}

} // namespace uniscope::d2
#endif
