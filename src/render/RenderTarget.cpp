/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/RenderTarget.h"

#include "GlCompat.h"

#include <algorithm>

namespace uniscope::d2 {

namespace {

GLenum glInternalFormat(RenderTargetFormat format) {
    switch (format) {
        case RenderTargetFormat::Rgba8: return GL_RGBA8;
        case RenderTargetFormat::R8:    return GL_R8;
    }
    return GL_RGBA8;
}

GLenum glFormat(RenderTargetFormat format) {
    switch (format) {
        case RenderTargetFormat::Rgba8: return GL_RGBA;
        case RenderTargetFormat::R8:    return GL_RED;
    }
    return GL_RGBA;
}

} // namespace

RenderTarget::~RenderTarget() {
    destroy();
}

bool RenderTarget::create(int width, int height, RenderTargetFormat format, int samples) {
    destroy();
    if (width <= 0 || height <= 0) {
        return false;
    }

    // Clamp samples to a sane range; 0 or 1 means single-sampled.
    const int requestedSamples = std::max(0, samples);
    const bool useMultisample = requestedSamples > 1;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Colour texture: always created, serves as the resolve target when MSAA is used,
    // or as the draw surface itself when single-sampled.
    glGenTextures(1, &colorTexture_);
    glBindTexture(GL_TEXTURE_2D, colorTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, glInternalFormat(format), width, height, 0, glFormat(format), GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (useMultisample) {
        // Multisampled renderbuffer is the actual draw surface.
        glGenRenderbuffers(1, &multisampleRbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, multisampleRbo_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, requestedSamples, glInternalFormat(format), width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleRbo_);

        // Separate resolve FBO that reads from the multisample RBO and blits to the texture.
        glGenFramebuffers(1, &resolveFbo_);
    } else {
        // Single-sampled: the texture *is* the draw surface.
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);
    }

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!complete) {
        destroy();
        return false;
    }

    width_ = width;
    height_ = height;
    samples_ = useMultisample ? requestedSamples : 0;
    format_ = format;
    return true;
}

void RenderTarget::destroy() {
    if (multisampleRbo_ != 0) {
        glDeleteRenderbuffers(1, &multisampleRbo_);
        multisampleRbo_ = 0;
    }
    if (resolveFbo_ != 0) {
        glDeleteFramebuffers(1, &resolveFbo_);
        resolveFbo_ = 0;
    }
    if (colorTexture_ != 0) {
        glDeleteTextures(1, &colorTexture_);
        colorTexture_ = 0;
    }
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    width_ = 0;
    height_ = 0;
    samples_ = 0;
    format_ = RenderTargetFormat::Rgba8;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void RenderTarget::unbind(int screenWidth, int screenHeight) const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screenWidth, screenHeight);
}

void RenderTarget::resolve() const {
    // No-op for single-sampled targets.
    if (samples_ <= 1 || resolveFbo_ == 0 || multisampleRbo_ == 0) {
        return;
    }

    // Bind resolve FBO with the colour texture attached.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);       // source: multisample RBO
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_); // dest: colour texture
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture_, 0);

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    GlDrawBufferColorAttachment0();

    glBlitFramebuffer(
        0, 0, width_, height_,
        0, 0, width_, height_,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST // Resolve uses nearest; the texture's own filter handles magnification.
    );

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    GlDrawBufferBack();
}

} // namespace uniscope::d2