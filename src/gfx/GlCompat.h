/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_GL_COMPAT_H
#define UNISCOPE_D2_GFX_GL_COMPAT_H

// Cross-platform GL header: desktop OpenGL 3.3 via GLAD, Android OpenGL ES 3.0
// via the NDK system headers. All renderer code includes this instead of
// <glad/glad.h> directly so the same sources compile for both.
//
// Desktop: GLAD_GL_EXT_texture_filter_anisotropic keeps its GLAD meaning.
// Android: it is emulated by GlHasAnisotropy(), which queries the EGL/ES
// extension string lazily (GLAD does not exist on Android).

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

typedef void* (*GLADloadproc)(const char* name);

inline int gladLoadGLLoader(GLADloadproc) {
    // System GLES needs no runtime loader; EGL already resolved the symbols.
    return 1;
}

namespace uniscope::d2 {

// Lazily probes GL_EXT_texture_filter_anisotropic. Safe to call with no
// context (returns false). Result is cached until GlInvalidateExtensionCache().
bool GlHasAnisotropy();
void GlInvalidateExtensionCache();

// Desktop GL's glDrawBuffer has no GLES 3.0 counterpart: a user framebuffer
// always draws into GL_COLOR_ATTACHMENT0 and the default framebuffer always
// draws into GL_BACK, so the desktop calls are no-ops here.
inline void GlDrawBufferColorAttachment0() {}
inline void GlDrawBufferBack() {}

} // namespace uniscope::d2

#define GLAD_GL_EXT_texture_filter_anisotropic (uniscope::d2::GlHasAnisotropy())

#else
#include <glad/glad.h>

namespace uniscope::d2 {

// Desktop GL selects the draw buffer explicitly; the GLES branch of this
// header provides the no-op equivalents.
inline void GlDrawBufferColorAttachment0() { glDrawBuffer(GL_COLOR_ATTACHMENT0); }
inline void GlDrawBufferBack() { glDrawBuffer(GL_BACK); }

} // namespace uniscope::d2
#endif

#endif
