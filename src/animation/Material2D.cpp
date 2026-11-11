/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "uniscope/d2/Material2D.h"

namespace uniscope::d2 {

Material2D::Material2D(MaterialPreset2D preset)
    : preset_(preset) {}

void Material2D::setCustomShaders(std::string vertexPath, std::string fragmentPath) {
    preset_ = MaterialPreset2D::Custom;
    customVertex_ = std::move(vertexPath);
    customFragment_ = std::move(fragmentPath);
}

} // namespace uniscope::d2
