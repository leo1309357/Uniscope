/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_SHADER_PROGRAM_H
#define UNISCOPE_D2_GFX_SHADER_PROGRAM_H

#include "GlCompat.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace uniscope::d2 {

class ShaderProgram {
public:
    ShaderProgram(const std::string& vertexPath, const std::string& fragmentPath);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    void use() const;
    // The set* calls deduplicate by value: re-uploading the value the program
    // already holds skips the driver call. A world frame flushes the sprite
    // batch a dozen times while almost every uniform (fog, light direction,
    // ambient) is set once per frame, so the redundant glUniform traffic was
    // the same class of waste the once-per-frame render-target query removed.
    void setFloat(const char* name, float value) const;
    void setInt(const char* name, int value) const;
    void setVec2(const char* name, float x, float y) const;
    void setVec3(const char* name, float x, float y, float z) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    // `count` vec3s, three floats each, in one upload. The per-value setters
    // above deduplicate by value, which is what makes them cheap to call every
    // flush; a whole table is uploaded only when the caller says it changed,
    // because comparing a table against its previous contents costs as much as
    // the upload it would save.
    void setVec3Array(const char* name, int count, const float* values) const;

    GLuint id() const;

private:
    static std::string readFile(const std::string& path);
    static GLuint compileShader(GLenum type, const std::string& source, const std::string& path);
    static void checkProgramLink(GLuint program);
    static std::string getShaderLog(GLuint shader);
    static std::string getProgramLog(GLuint program);

    struct UniformSlot {
        GLint location = -1;
        // Last value uploaded through a set* call. kind distinguishes an int
        // write from a float write of the same name so one can never suppress
        // the other; components == 0 means nothing has been uploaded yet.
        // NaN never compares equal, so a NaN value simply always re-uploads.
        float value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        std::uint8_t components = 0;
        std::uint8_t kind = 0; // 0 = float, 1 = int
    };

    UniformSlot& uniformSlot(const char* name) const;

    GLuint id_ = 0;
    mutable std::unordered_map<std::string, UniformSlot> uniforms_;
};

} // namespace uniscope::d2

#endif
