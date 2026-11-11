/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#include "ShaderProgram.h"

#include "AndroidAssetReader.h"
#include "GlCompat.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace uniscope::d2 {

namespace {

std::string directoryOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) {
        return std::string();
    }
    return path.substr(0, slash + 1);
}

bool isAbsoluteLike(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return path.size() > 1 && path[1] == ':';
}

std::string readRawFile(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path));
    if (file) {
        std::stringstream stream;
        stream << file.rdbuf();
        if (!file.good() && !file.eof()) {
            throw std::runtime_error("Failed to read shader file: " + path);
        }
        return stream.str();
    }

    // Android: shaders live inside the APK; fall back to AAssetManager.
    const std::string assetText = AndroidAssetReadText(path);
    if (!assetText.empty()) {
        return assetText;
    }

    throw std::runtime_error("Failed to open shader file: " + path);
}

// Desktop GLSL (`#version 330 core`) and GLES GLSL (`#version 300 es`) share
// the same language subset used by this renderer (in/out, texture(),
// textureSize(), gl_FragCoord). Rewriting the version header at load time
// keeps a single shader tree for all four platforms instead of forking one.
#if defined(__ANDROID__)
std::string adaptShaderForGles(std::string source, bool isFragment) {
    constexpr const char* kDesktopVersion = "#version 330 core";
    if (source.compare(0, std::strlen(kDesktopVersion), kDesktopVersion) != 0) {
        return source;
    }
    std::string adapted = "#version 300 es\n";
    if (isFragment) {
        // highp, not mediump: the world batch shader feeds world-space
        // coordinates and large-atlas UVs through its varyings, and mediump's
        // 10-bit mantissa visibly tears that math apart (sprites sample the
        // wrong texels or vanish outright - the classic "UI renders, world is
        // a void" Android bug). highp is mandatory in ES 3.0 fragment
        // shaders, so this costs correctness nothing it didn't already have.
        adapted += "precision highp float;\nprecision highp int;\nprecision mediump sampler2D;\n";
    }
    adapted += source.substr(std::strlen(kDesktopVersion));
    return adapted;
}
#endif

// Recursively expands `#include "relative/path.glsl"` directives so a single
// GLSL source can share helper code without relying on driver extensions.
// Includes resolve relative to the including file and are cycle-checked.
std::string readFileWithIncludes(const std::string& path, std::set<std::string>& active) {
    if (!active.insert(path).second) {
        throw std::runtime_error("Circular shader include detected at: " + path);
    }

    const std::string source = readRawFile(path);
    const std::string baseDir = directoryOf(path);

    std::istringstream lines(source);
    std::string output;
    output.reserve(source.size());

    std::string line;
    while (std::getline(lines, line)) {
        // Only look for directives outside of `//` comments so commented-out
        // or illustrative includes are never expanded.
        const std::size_t comment = line.find("//");
        const std::string code = comment == std::string::npos ? line : line.substr(0, comment);
        const std::size_t hash = code.find('#');
        bool isInclude = false;
        if (hash != std::string::npos) {
            std::size_t cursor = hash + 1;
            while (cursor < code.size() && (code[cursor] == ' ' || code[cursor] == '\t')) {
                ++cursor;
            }
            const std::string directive = "include";
            if (code.compare(cursor, directive.size(), directive) == 0) {
                cursor += directive.size();
                while (cursor < code.size() && (code[cursor] == ' ' || code[cursor] == '\t')) {
                    ++cursor;
                }
                if (cursor < code.size() && code[cursor] == '"') {
                    const std::size_t end = code.find('"', cursor + 1);
                    if (end != std::string::npos) {
                        const std::string target = code.substr(cursor + 1, end - cursor - 1);
                        const std::string resolved = isAbsoluteLike(target) ? target : baseDir + target;
                        output += readFileWithIncludes(resolved, active);
                        output += '\n';
                        isInclude = true;
                    }
                }
            }
        }

        if (!isInclude) {
            output += line;
            output += '\n';
        }
    }

    active.erase(path);
    return output;
}

} // namespace

ShaderProgram::ShaderProgram(const std::string& vertexPath, const std::string& fragmentPath) {
    std::string vertexCode = readFile(vertexPath);
    std::string fragmentCode = readFile(fragmentPath);
#if defined(__ANDROID__)
    vertexCode = adaptShaderForGles(std::move(vertexCode), false);
    fragmentCode = adaptShaderForGles(std::move(fragmentCode), true);
#endif

    GLuint vertex = 0;
    GLuint fragment = 0;

    try {
        vertex = compileShader(GL_VERTEX_SHADER, vertexCode, vertexPath);
        fragment = compileShader(GL_FRAGMENT_SHADER, fragmentCode, fragmentPath);

        id_ = glCreateProgram();
        glAttachShader(id_, vertex);
        glAttachShader(id_, fragment);
        glLinkProgram(id_);
        checkProgramLink(id_);

        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }
    catch (...) {
        if (vertex != 0) {
            glDeleteShader(vertex);
        }
        if (fragment != 0) {
            glDeleteShader(fragment);
        }
        if (id_ != 0) {
            glDeleteProgram(id_);
            id_ = 0;
        }
        throw;
    }
}

ShaderProgram::~ShaderProgram() {
    if (id_ != 0) {
        glDeleteProgram(id_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : id_(std::exchange(other.id_, 0)),
      uniforms_(std::move(other.uniforms_)) {}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            glDeleteProgram(id_);
        }

        id_ = std::exchange(other.id_, 0);
        uniforms_ = std::move(other.uniforms_);
    }

    return *this;
}

void ShaderProgram::use() const {
    glUseProgram(id_);
}

void ShaderProgram::setFloat(const char* name, float value) const {
    UniformSlot& slot = uniformSlot(name);
    if (slot.components == 1 && slot.kind == 0 && slot.value[0] == value) {
        return;
    }
    glUniform1f(slot.location, value);
    slot.value[0] = value;
    slot.components = 1;
    slot.kind = 0;
}

void ShaderProgram::setInt(const char* name, int value) const {
    UniformSlot& slot = uniformSlot(name);
    const float asFloat = static_cast<float>(value);
    if (slot.components == 1 && slot.kind == 1 && slot.value[0] == asFloat) {
        return;
    }
    glUniform1i(slot.location, value);
    slot.value[0] = asFloat;
    slot.components = 1;
    slot.kind = 1;
}

void ShaderProgram::setVec2(const char* name, float x, float y) const {
    UniformSlot& slot = uniformSlot(name);
    if (slot.components == 2 && slot.kind == 0 && slot.value[0] == x && slot.value[1] == y) {
        return;
    }
    glUniform2f(slot.location, x, y);
    slot.value[0] = x;
    slot.value[1] = y;
    slot.components = 2;
    slot.kind = 0;
}

void ShaderProgram::setVec3(const char* name, float x, float y, float z) const {
    UniformSlot& slot = uniformSlot(name);
    if (slot.components == 3 && slot.kind == 0
        && slot.value[0] == x && slot.value[1] == y && slot.value[2] == z) {
        return;
    }
    glUniform3f(slot.location, x, y, z);
    slot.value[0] = x;
    slot.value[1] = y;
    slot.value[2] = z;
    slot.components = 3;
    slot.kind = 0;
}

void ShaderProgram::setVec4(const char* name, float x, float y, float z, float w) const {
    UniformSlot& slot = uniformSlot(name);
    if (slot.components == 4 && slot.kind == 0
        && slot.value[0] == x && slot.value[1] == y && slot.value[2] == z && slot.value[3] == w) {
        return;
    }
    glUniform4f(slot.location, x, y, z, w);
    slot.value[0] = x;
    slot.value[1] = y;
    slot.value[2] = z;
    slot.value[3] = w;
    slot.components = 4;
    slot.kind = 0;
}

void ShaderProgram::setVec3Array(const char* name, int count, const float* values) const {
    // Not deduplicated against the previous contents: a table of a dozen
    // entries costs as much to compare as to upload, and the caller only calls
    // this when the table actually changed.
    if (count <= 0 || values == nullptr) {
        return;
    }
    const UniformSlot& slot = uniformSlot(name);
    glUniform3fv(slot.location, count, values);
}

GLuint ShaderProgram::id() const {
    return id_;
}

ShaderProgram::UniformSlot& ShaderProgram::uniformSlot(const char* name) const {
    const std::string key(name);
    const auto cached = uniforms_.find(key);
    if (cached != uniforms_.end()) {
        return cached->second;
    }

    UniformSlot slot;
    slot.location = glGetUniformLocation(id_, name);
    return uniforms_.emplace(key, slot).first->second;
}

std::string ShaderProgram::readFile(const std::string& path) {
    std::set<std::string> active;
    return readFileWithIncludes(path, active);
}

GLuint ShaderProgram::compileShader(GLenum type, const std::string& source, const std::string& path) {
    const GLuint shader = glCreateShader(type);
    const char* shaderCode = source.c_str();
    glShaderSource(shader, 1, &shaderCode, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        const std::string log = getShaderLog(shader);
        glDeleteShader(shader);
        throw std::runtime_error("Failed to compile shader '" + path + "':\n" + log);
    }

    return shader;
}

void ShaderProgram::checkProgramLink(GLuint program) {
    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        const std::string log = getProgramLog(program);
        throw std::runtime_error("Failed to link shader program:\n" + log);
    }
}

std::string ShaderProgram::getShaderLog(GLuint shader) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    if (length > 0) {
        glGetShaderInfoLog(shader, length, nullptr, log.data());
    }
    return log;
}

std::string ShaderProgram::getProgramLog(GLuint program) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length), '\0');
    if (length > 0) {
        glGetProgramInfoLog(program, length, nullptr, log.data());
    }
    return log;
}

} // namespace uniscope::d2
