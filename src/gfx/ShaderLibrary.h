/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_SHADER_LIBRARY_H
#define UNISCOPE_D2_GFX_SHADER_LIBRARY_H

#include "ShaderId.h"
#include "ShaderProgram.h"

#include <memory>
#include <unordered_map>

namespace uniscope::d2 {

class ShaderLibrary {
public:
    ShaderProgram& program(ShaderId id);
    bool has(ShaderId id) const;
    void clear();

private:
    std::unique_ptr<ShaderProgram> loadProgram(ShaderId id);

    std::unordered_map<ShaderId, std::unique_ptr<ShaderProgram>> programs_;
};

} // namespace uniscope::d2

#endif
