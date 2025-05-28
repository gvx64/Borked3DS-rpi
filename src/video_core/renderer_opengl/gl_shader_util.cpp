// Copyright 2014 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <string>
#include <vector>
#include <glad/gl.h>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/gl_vars.h"
#include "video_core/shader/generator/shader_uniforms.h"

namespace OpenGL {

GLuint LoadShader(std::string_view source, GLenum type) {
    std::string preamble;

    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (GLES) {
#ifdef __ANDROID__

        if (majorVersion == 3 && minorVersion == 1) {
            preamble = R"(#version 310 es

#if defined(GL_ANDROID_extension_pack_es31a)
#extension GL_ANDROID_extension_pack_es31a : enable
#endif // defined(GL_ANDROID_extension_pack_es31a)

#if defined(GL_EXT_geometry_shader)
#extension GL_EXT_geometry_shader : enable
#endif //defined(GL_EXT_geometry_shader)

#if defined(GL_EXT_separate_shader_objects)
#extension GL_EXT_separate_shader_objects : enable
#endif //defined(GL_EXT_separate_shader_objects)

#if defined(GL_EXT_texture_buffer)
#extension GL_EXT_texture_buffer : enable
#endif //defined(GL_EXT_texture_buffer)

#if defined(GL_OES_texture_buffer) //gvx64
#extension GL_OES_texture_buffer : enable //gvx64
#endif //defined(GL_OES_texture_buffer) - gvx64

#if defined(GL_EXT_texture_storage)
#extension GL_EXT_texture_storage : enable
#endif //defined(GL_EXT_texture_storage)

#if defined(GL_EXT_clip_cull_distance)
#extension GL_EXT_clip_cull_distance : enable
#endif // defined(GL_EXT_clip_cull_distance)

#if defined(GL_EXT_shader_image_load_store)
#extension GL_EXT_shader_image_load_store : enable
#endif // defined(GL_EXT_shader_image_load_store)

#if defined(GL_EXT_texture_shadow_lod)
#extension GL_EXT_texture_shadow_lod : enable
#endif // defined(GL_EXT_texture_shadow_lod)

#if defined(GL_ARB_explicit_uniform_location)
#extension GL_ARB_explicit_uniform_location : enable
#endif // defined(GL_ARB_explicit_uniform_location)
)";

        } else {
            preamble = R"(#version 320 es

#if defined(GL_ANDROID_extension_pack_es31a)
#extension GL_ANDROID_extension_pack_es31a : enable
#endif // defined(GL_ANDROID_extension_pack_es31a)

#if defined(GL_EXT_clip_cull_distance)
#extension GL_EXT_clip_cull_distance : enable
#endif // defined(GL_EXT_clip_cull_distance)
)";
        }

#else
        if (majorVersion == 3 && minorVersion == 1) {
            preamble = "#version 310 es\n"
                       "#if defined(GL_EXT_geometry_shader)\n"
                       "#extension GL_EXT_geometry_shader : enable\n"
                       "#endif //defined(GL_EXT_geometry_shader)\n"
                       "#if defined(GL_EXT_texture_buffer)\n"
                       "#extension GL_EXT_texture_buffer : enable\n"
                       "#endif //defined(GL_EXT_texture_buffer)\n"
                       "#if defined(GL_OES_texture_buffer)\n" //gvx64
                       "#extension GL_OES_texture_buffer : enable\n" //gvx64
                       "#endif //defined(GL_OES_texture_buffer)\n" //gvx64
                       "#if defined(GL_EXT_texture_storage)\n"
                       "#extension GL_EXT_texture_storage : enable\n"
                       "#endif //defined(GL_EXT_texture_storage)\n"
                       "#if defined(GL_EXT_separate_shader_objects)\n"
                       "#extension GL_EXT_separate_shader_objects : enable\n"
                       "#endif //defined(GL_EXT_separate_shader_objects)\n"
                       "#if defined(GL_EXT_clip_cull_distance)\n"
                       "#extension GL_EXT_clip_cull_distance : enable\n"
                       "#endif //defined(GL_EXT_clip_cull_distance)\n"
                       "#if defined(GL_EXT_clip_cull_distance)\n"
                       "#extension GL_EXT_clip_cull_distance : enable\n"
                       "#endif // defined(GL_EXT_clip_cull_distance)\n"
                       "#if defined(GL_EXT_texture_shadow_lod)\n"
                       "#extension GL_EXT_texture_shadow_lod : enable\n"
                       "#endif // defined(GL_EXT_texture_shadow_lod)\n"
                       "#if defined(GL_EXT_shader_image_load_store)\n"
                       "#extension GL_EXT_shader_image_load_store : enable\n"
                       "#endif // defined(GL_EXT_shader_image_load_store)\n"
                       "#if defined(GL_ARB_explicit_uniform_location)\n"
                       "#extension GL_ARB_explicit_uniform_location : enable\n"
                       "#endif // defined(GL_ARB_explicit_uniform_location)\n";
        } else {
            preamble = "#version 320 es\n"
                       "#if defined(GL_EXT_clip_cull_distance)\n"
                       "#extension GL_EXT_clip_cull_distance : enable\n"
                       "#endif //defined(GL_EXT_clip_cull_distance)\n";
        }
#endif
    } else {
        preamble = "#version 430 core\n"
                   "#if defined(GL_ARB_shader_image_load_store)\n"
                   "#extension GL_ARB_shader_image_load_store : enable\n"
                   "#endif //defined(GL_ARB_shader_image_load_store)\n";
    }

    std::string_view debug_type;

    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
        switch (type) {
        case GL_VERTEX_SHADER:
            debug_type = "vertex";
            break;
        case GL_GEOMETRY_SHADER_EXT:
            debug_type = "geometry";
            break;
        case GL_FRAGMENT_SHADER:
            debug_type = "fragment";
            break;
        default:
            UNREACHABLE();
        }
    } else {
        switch (type) {
        case GL_VERTEX_SHADER:
            debug_type = "vertex";
            break;
        case GL_GEOMETRY_SHADER:
            debug_type = "geometry";
            break;
        case GL_FRAGMENT_SHADER:
            debug_type = "fragment";
            break;
        default:
            UNREACHABLE();
        }
    }
    std::array<const GLchar*, 2> src_arr{preamble.data(), source.data()};
    std::array<GLint, 2> lengths{static_cast<GLint>(preamble.size()),
                                 static_cast<GLint>(source.size())};
    GLuint shader_id = glCreateShader(type);
    glShaderSource(shader_id, static_cast<GLsizei>(src_arr.size()), src_arr.data(), lengths.data());
    LOG_DEBUG(Render_OpenGL, "Compiling {} shader...", debug_type);
    glCompileShader(shader_id);

    GLint result = GL_FALSE;
    GLint info_log_length;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &result);
    glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &info_log_length);

    if (info_log_length > 1) {
        std::vector<char> shader_error(info_log_length);
        glGetShaderInfoLog(shader_id, info_log_length, nullptr, &shader_error[0]);
        if (result == GL_TRUE) {
            LOG_DEBUG(Render_OpenGL, "{}", &shader_error[0]);
        } else {
            LOG_ERROR(Render_OpenGL, "Error compiling {} shader:\n{}", debug_type,
                      &shader_error[0]);
            LOG_ERROR(Render_OpenGL, "Shader source code:\n{}{}", src_arr[0], src_arr[1]);
        }
    }
    return shader_id;
}

GLuint LoadProgram(bool separable_program, std::span<const GLuint> shaders) {
    // Add validation for input shaders
    if (shaders.empty()) {
        LOG_ERROR(Render_OpenGL, "No shaders provided to link");
        return 0;
    }

    GLuint program_id = glCreateProgram();
    if (program_id == 0) {
        LOG_ERROR(Render_OpenGL, "Failed to create program object");
        return 0;
    }

    // Link the program
    LOG_DEBUG(Render_OpenGL, "Linking program...");

    for (GLuint shader : shaders) {
        if (shader != 0) {
            glAttachShader(program_id, shader);
        }
    }

    if (separable_program && GLAD_GL_EXT_separate_shader_objects) {
        glProgramParameteri(program_id, GL_PROGRAM_SEPARABLE, GL_TRUE);
    }

    glProgramParameteri(program_id, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);

    glLinkProgram(program_id);

    // Check the program
    GLint result = GL_FALSE;
    GLint info_log_length;
    glGetProgramiv(program_id, GL_LINK_STATUS, &result);
    glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &info_log_length);

    if (info_log_length > 1) {
        std::vector<char> program_error(info_log_length);
        glGetProgramInfoLog(program_id, info_log_length, nullptr, &program_error[0]);
        if (result == GL_TRUE) {
            LOG_DEBUG(Render_OpenGL, "{}", &program_error[0]);
        } else {
            LOG_ERROR(Render_OpenGL, "Error linking shader:\n{}", &program_error[0]);
        }
    }

    // Add program validation after linking
    if (result == GL_TRUE) {
        glValidateProgram(program_id);
        GLint validate_status;
        glGetProgramiv(program_id, GL_VALIDATE_STATUS, &validate_status);
        if (validate_status != GL_TRUE) {
            GLint validate_log_length;
            glGetProgramiv(program_id, GL_INFO_LOG_LENGTH, &validate_log_length);
            if (validate_log_length > 1) {
                std::vector<char> validate_error(validate_log_length);
                glGetProgramInfoLog(program_id, validate_log_length, nullptr, &validate_error[0]);
                LOG_ERROR(Render_OpenGL, "Program validation failed:\n{}", &validate_error[0]);
            }
            glDeleteProgram(program_id);
            return 0;
        }
    }

    ASSERT_MSG(result == GL_TRUE, "Shader not linked");

    for (GLuint shader : shaders) {
        if (shader != 0) {
            glDetachShader(program_id, shader);
        }
    }

    return program_id;
}

} // namespace OpenGL
