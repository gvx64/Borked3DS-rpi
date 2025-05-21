// Copyright 2022 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/gl.h>

#include "common/profiling.h"
#include "common/settings.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_vars.h"

namespace OpenGL {

namespace detail {

bool IsGLES() {
    if (Settings::values.use_gles.GetValue() || OpenGL::GLES) {
        return true;
    } else {
        return false;
    }
}

bool SupportsTextureStorage() {
    static bool supports_texture_storage = [] {
        GLint majorVersion = 0, minorVersion = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

        if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
            return GLAD_GL_ES_VERSION_3_0 || GLAD_GL_EXT_texture_storage;
        } else {
            return true;
        }
    }();
    return supports_texture_storage;
}

} // namespace detail

void OGLRenderbuffer::Create() {
    if (handle != 0) {
        return;
    }

    glGenRenderbuffers(1, &handle);
}

void OGLRenderbuffer::Release() {
    if (handle == 0) {
        return;
    }

    glDeleteRenderbuffers(1, &handle);
    OpenGLState::GetCurState().ResetRenderbuffer(handle).Apply();
    handle = 0;
}

void OGLTexture::Create() {
    if (handle != 0) {
        return;
    }

    glGenTextures(1, &handle);
}

void OGLTexture::Release() {
    if (handle == 0) {
        return;
    }

    glDeleteTextures(1, &handle);
    OpenGLState::GetCurState().ResetTexture(handle).Apply();
    handle = 0;
}

void OGLTexture::Allocate(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width,
                          GLsizei height, GLsizei depth) {
    if (is_gles) {
        AllocateGLES(target, levels, internalformat, width, height, depth);
        return;
    }

    // Original desktop GL implementation
    GLuint old_tex = OpenGLState::GetCurState().texture_units[0].texture_2d;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, handle);

    switch (target) {
    case GL_TEXTURE_1D:
    case GL_TEXTURE:
        glTexStorage1D(target, levels, internalformat, width);
        break;
    case GL_TEXTURE_2D:
    case GL_TEXTURE_1D_ARRAY:
    case GL_TEXTURE_RECTANGLE:
    case GL_TEXTURE_CUBE_MAP:
        glTexStorage2D(target, levels, internalformat, width, height);
        break;
    case GL_TEXTURE_3D:
    case GL_TEXTURE_2D_ARRAY:
    case GL_TEXTURE_CUBE_MAP_ARRAY:
        glTexStorage3D(target, levels, internalformat, width, height, depth);
        break;
    }

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, old_tex);
}

void OGLTexture::AllocateGLES(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width,
                              GLsizei height, GLsizei depth) {
    GLuint old_tex = OpenGLState::GetCurState().texture_units[0].texture_2d;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, handle);

    // Convert desktop GL internal formats to GLES compatible ones
    GLenum gles_format = internalformat;
    GLenum gles_type = GL_UNSIGNED_BYTE;

    switch (internalformat) {
    case GL_RGBA8:
        gles_format = GL_RGBA;
        gles_type = GL_UNSIGNED_BYTE;
        break;
    case GL_RGB8:
        gles_format = GL_RGB;
        gles_type = GL_UNSIGNED_BYTE;
        break;
    case GL_DEPTH24_STENCIL8:
        gles_format = GL_DEPTH_STENCIL_OES;
        gles_type = GL_UNSIGNED_INT_24_8_OES;
        break;
        // Add more format conversions as needed
    }

    if (detail::SupportsTextureStorage()) {
        // Use texture storage if available
        switch (target) {
        case GL_TEXTURE_2D:
        case GL_TEXTURE_CUBE_MAP:
            glTexStorage2D(target, levels, internalformat, width, height);
            break;
        case GL_TEXTURE_3D:
        case GL_TEXTURE_2D_ARRAY:
            glTexStorage3D(target, levels, internalformat, width, height, depth);
            break;
        }
    } else {
        // Fall back to legacy texture allocation
        switch (target) {
        case GL_TEXTURE_2D:
            for (GLsizei level = 0; level < levels; ++level) {
                glTexImage2D(target, level, gles_format, width >> level, height >> level, 0,
                             gles_format, gles_type, nullptr);
            }
            break;
        case GL_TEXTURE_3D:
            for (GLsizei level = 0; level < levels; ++level) {
                glTexImage3D(target, level, gles_format, width >> level, height >> level,
                             depth >> level, 0, gles_format, gles_type, nullptr);
            }
            break;
        }
    }

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, old_tex);
}

void OGLSampler::Create() {
    if (handle != 0) {
        return;
    }

    glGenSamplers(1, &handle);
}

void OGLSampler::Release() {
    if (handle == 0) {
        return;
    }

    glDeleteSamplers(1, &handle);
    OpenGLState::GetCurState().ResetSampler(handle).Apply();
    handle = 0;
}

void OGLShader::Create(std::string_view source, GLenum type) {
    if (handle != 0)
        return;
    if (source.empty())
        return;

    handle = LoadShader(source, type);
}

void OGLShader::Release() {
    if (handle == 0)
        return;

    glDeleteShader(handle);
    handle = 0;
}

void OGLProgram::Create(bool separable_program, std::span<const GLuint> shaders) {
    if (handle != 0)
        return;

    handle = LoadProgram(separable_program, shaders);
}

void OGLProgram::Create(std::string_view vert_shader, std::string_view frag_shader) {
    OGLShader vert, frag;
    vert.Create(vert_shader, GL_VERTEX_SHADER);
    frag.Create(frag_shader, GL_FRAGMENT_SHADER);

    const std::array shaders{vert.handle, frag.handle};
    Create(false, shaders);
}

void OGLProgram::Release() {
    if (handle == 0)
        return;

    glDeleteProgram(handle);
    OpenGLState::GetCurState().ResetProgram(handle).Apply();
    handle = 0;
}

void OGLPipeline::Create() {
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (handle != 0)
        return;

    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
        glGenProgramPipelinesEXT(1, &handle);
    } else {
        glGenProgramPipelines(1, &handle);
    }
}

void OGLPipeline::Release() {
    if (handle == 0)
        return;

    glDeleteProgramPipelines(1, &handle);
    OpenGLState::GetCurState().ResetPipeline(handle).Apply();
    handle = 0;
}

void OGLBuffer::Create() {
    if (handle != 0)
        return;

    glGenBuffers(1, &handle);
}

void OGLBuffer::Release() {
    if (handle == 0)
        return;

    glDeleteBuffers(1, &handle);
    OpenGLState::GetCurState().ResetBuffer(handle).Apply();
    handle = 0;
}

void OGLVertexArray::Create() {
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (handle != 0)
        return;

    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
        glGenVertexArraysOES(1, &handle);
    } else {
        glGenVertexArrays(1, &handle);
    }
}

void OGLVertexArray::Release() {
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (handle == 0)
        return;

    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
        if (GLAD_GL_OES_vertex_array_object) {
            glDeleteVertexArraysOES(1, &handle);
        } else {
            glDeleteVertexArrays(1, &handle);
        }
    } else {
        glDeleteVertexArrays(1, &handle);
    }

    OpenGLState::GetCurState().ResetVertexArray(handle).Apply();
    handle = 0;
}
/*void OGLVertexArray::Release() { //gvx64
    if (handle == 0)
        return;

    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
        // VAOs not supported or not used on GLES < 3.2
        handle = 0;
        return;
    }

    // Delete VAO using appropriate function
    if (OpenGL::GLES && GLAD_GL_OES_vertex_array_object) {
        glDeleteVertexArraysOES(1, &handle);
    } else {
        glDeleteVertexArrays(1, &handle);
    }

    OpenGLState::GetCurState().ResetVertexArray(handle).Apply();
    handle = 0;
} //gvx64*/

void OGLFramebuffer::Create() {
    if (handle != 0)
        return;

    glGenFramebuffers(1, &handle);
}

void OGLFramebuffer::Release() {
    if (handle == 0)
        return;

    glDeleteFramebuffers(1, &handle);
    OpenGLState::GetCurState().ResetFramebuffer(handle).Apply();
    handle = 0;
}

} // namespace OpenGL
