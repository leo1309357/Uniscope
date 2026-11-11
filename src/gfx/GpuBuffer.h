/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_GPU_BUFFER_H
#define UNISCOPE_D2_GFX_GPU_BUFFER_H

#include "GlCompat.h"

#include <utility>

namespace uniscope::d2 {

class VertexArray {
public:
    VertexArray() {
        glGenVertexArrays(1, &id_);
    }

    ~VertexArray() {
        if (id_ != 0) {
            glDeleteVertexArrays(1, &id_);
        }
    }

    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;

    VertexArray(VertexArray&& other) noexcept
        : id_(std::exchange(other.id_, 0)) {}

    VertexArray& operator=(VertexArray&& other) noexcept {
        if (this != &other) {
            if (id_ != 0) {
                glDeleteVertexArrays(1, &id_);
            }
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    GLuint id() const {
        return id_;
    }

private:
    GLuint id_ = 0;
};

class VertexBuffer {
public:
    VertexBuffer() {
        glGenBuffers(1, &id_);
    }

    ~VertexBuffer() {
        if (id_ != 0) {
            glDeleteBuffers(1, &id_);
        }
    }

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    VertexBuffer(VertexBuffer&& other) noexcept
        : id_(std::exchange(other.id_, 0)) {}

    VertexBuffer& operator=(VertexBuffer&& other) noexcept {
        if (this != &other) {
            if (id_ != 0) {
                glDeleteBuffers(1, &id_);
            }
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    GLuint id() const {
        return id_;
    }

private:
    GLuint id_ = 0;
};

} // namespace uniscope::d2

#endif
