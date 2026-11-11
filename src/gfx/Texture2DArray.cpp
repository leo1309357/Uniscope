/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "Texture2DArray.h"

#include "TextureFilter.h"

#include <algorithm>

namespace uniscope::d2 {

namespace {

bool layerUsable(const std::vector<unsigned char>& pixels, int width, int height) {
    return width > 0 && height > 0
        && static_cast<long long>(pixels.size()) >= static_cast<long long>(width) * height * 4;
}

} // namespace

Texture2DArray::~Texture2DArray() {
    if (textureId_ != 0) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
}

Texture2DArray::Texture2DArray(Texture2DArray&& other) noexcept
    : textureId_(other.textureId_),
      width_(other.width_),
      height_(other.height_),
      layers_(other.layers_) {
    other.textureId_ = 0;
    other.width_ = 0;
    other.height_ = 0;
    other.layers_ = 0;
}

Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept {
    if (this != &other) {
        if (textureId_ != 0) {
            glDeleteTextures(1, &textureId_);
        }

        textureId_ = other.textureId_;
        width_ = other.width_;
        height_ = other.height_;
        layers_ = other.layers_;
        other.textureId_ = 0;
        other.width_ = 0;
        other.height_ = 0;
        other.layers_ = 0;
    }
    return *this;
}

void Texture2DArray::applyFilter(uniscope::d2::TextureQuality quality) const {
    applyTextureFilter(textureId_, quality, true, GL_TEXTURE_2D_ARRAY);
}

Texture2DArray Texture2DArray::loadTilingFromLayers(
    const std::vector<std::vector<unsigned char>>& layers,
    int width,
    int height,
    uniscope::d2::TextureQuality quality) {
    Texture2DArray texture;
    if (layers.empty() || layers.size() > 512) {
        return texture;
    }
    // Every layer has to be the size the array was declared at. A short layer
    // would be read past its end during the upload, and a long one would put the
    // next material's texels in this one - both silent, and both exactly the kind
    // of cross-material bleed that per-layer mipmaps exist to prevent. Rejecting
    // the whole array instead leaves the caller with an invalid texture, which the
    // renderer already treats as "no detail map".
    for (const std::vector<unsigned char>& layer : layers) {
        if (!layerUsable(layer, width, height)) {
            return {};
        }
    }

    glGenTextures(1, &texture.textureId_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture.textureId_);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    const auto count = static_cast<GLsizei>(layers.size());
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_RGBA8,
        width,
        height,
        count,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    for (GLsizei layer = 0; layer < count; ++layer) {
        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0,
            0,
            layer,
            width,
            height,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            layers[static_cast<std::size_t>(layer)].data());
    }

    // GL_REPEAT is per-layer here, so the mip chain of one layer never reaches
    // another. `glGenerateMipmap` on a 2D array is defined to reduce each layer
    // independently, which is the whole reason this is an array and not a grid in
    // one 2D image.
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    texture.applyFilter(quality);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    texture.width_ = width;
    texture.height_ = height;
    texture.layers_ = static_cast<int>(count);
    return texture;
}

Texture2DArray Texture2DArray::loadFromLayers(
    const std::vector<std::vector<unsigned char>>& layers,
    int width,
    int height,
    uniscope::d2::TextureQuality quality) {
    Texture2DArray texture;
    if (layers.empty() || layers.size() > 512) {
        return texture;
    }
    for (const std::vector<unsigned char>& layer : layers) {
        if (!layerUsable(layer, width, height)) {
            return {};
        }
    }

    glGenTextures(1, &texture.textureId_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture.textureId_);
    // Non-tiling: clamp to edge so building edges don't wrap
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    const auto count = static_cast<GLsizei>(layers.size());
    glTexImage3D(
        GL_TEXTURE_2D_ARRAY,
        0,
        GL_RGBA8,
        width,
        height,
        count,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    for (GLsizei layer = 0; layer < count; ++layer) {
        glTexSubImage3D(
            GL_TEXTURE_2D_ARRAY,
            0,
            0,
            0,
            layer,
            width,
            height,
            1,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            layers[static_cast<std::size_t>(layer)].data());
    }

    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

    texture.applyFilter(quality);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    texture.width_ = width;
    texture.height_ = height;
    texture.layers_ = static_cast<int>(count);
    return texture;
}

} // namespace uniscope::d2