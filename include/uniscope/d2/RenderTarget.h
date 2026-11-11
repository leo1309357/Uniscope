/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_RENDER_TARGET_H
#define UNISCOPE_D2_RENDER_TARGET_H

#include <cstdint>

namespace uniscope::d2 {

// Opaque name of a texture created by this renderer. The value is whatever the
// active graphics API calls a texture (a GL name here), but callers never need
// to know that: they pass it back to the renderer as a handle.
struct TextureHandle {
    std::uint32_t id = 0;

    bool valid() const { return id != 0; }
    bool operator==(TextureHandle other) const { return id == other.id; }
    bool operator!=(TextureHandle other) const { return id != other.id; }
};

// Colour format of an offscreen target. The shadow map stores one scalar
// (darkness), so R8 holds it in a quarter of the bandwidth RGBA8 did; the
// scene target carries colour and stays RGBA8.
enum class RenderTargetFormat {
    Rgba8,
    R8
};

// An offscreen colour target. Colour only: nothing in this renderer tests
// depth, so the depth renderbuffer every target used to allocate was four
// bytes a pixel of memory nobody ever read.
//
// With `samples` > 1 the drawn surface is a multisampled renderbuffer and the
// texture is its resolve: draw with bind(), then resolve() once before sampling
// colorTexture(). With `samples` <= 1 the texture is the drawn surface itself
// and resolve() is a no-op, so callers need not know which kind they hold.
class RenderTarget {
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    bool create(
        int width,
        int height,
        RenderTargetFormat format = RenderTargetFormat::Rgba8,
        int samples = 0);
    void destroy();
    void bind() const;
    void unbind(int screenWidth, int screenHeight) const;
    // Copies the multisampled surface into the resolve texture. Leaves the
    // read/draw framebuffer bindings on this target; the caller rebinds its own
    // destination afterwards as it already must after bind().
    void resolve() const;

    bool valid() const { return fbo_ != 0; }
    TextureHandle colorTexture() const { return TextureHandle{colorTexture_}; }
    int width() const { return width_; }
    int height() const { return height_; }
    // The sample count actually in use: 0 or 1 when the target is single
    // sampled, which can be less than was asked for if the driver refused.
    int samples() const { return samples_; }
    RenderTargetFormat format() const { return format_; }

private:
    // GL object names. Kept as plain integers so this header does not pull a
    // platform graphics header into every consumer.
    unsigned int fbo_ = 0;
    unsigned int resolveFbo_ = 0;
    unsigned int colorTexture_ = 0;
    unsigned int multisampleRbo_ = 0;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 0;
    RenderTargetFormat format_ = RenderTargetFormat::Rgba8;
};

} // namespace uniscope::d2

#endif
