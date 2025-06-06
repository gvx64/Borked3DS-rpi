// Copyright 2022 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/alignment.h"
#include "common/assert.h"
#include "common/profiling.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_stream_buffer.h"

namespace OpenGL {

OGLStreamBuffer::OGLStreamBuffer(Driver& driver, GLenum target, GLsizeiptr size,
                                 bool prefer_coherent)
    : gl_target(target), buffer_size(size) {
    gl_buffer.Create();
    glBindBuffer(gl_target, gl_buffer.handle);

    GLsizeiptr allocate_size = size;
    if (driver.HasBug(DriverBug::VertexArrayOutOfBound) && target == GL_ARRAY_BUFFER) {
        allocate_size = allocate_size * 2;
    }

    if (GLAD_GL_ARB_buffer_storage) {
        persistent = true;
        coherent = prefer_coherent;
        GLbitfield flags =
            GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | (coherent ? GL_MAP_COHERENT_BIT : 0);
        glBufferStorage(gl_target, allocate_size, nullptr, flags);
        mapped_ptr = static_cast<u8*>(glMapBufferRange(
            gl_target, 0, buffer_size, flags | (coherent ? 0 : GL_MAP_FLUSH_EXPLICIT_BIT)));
    } else {
        glBufferData(gl_target, allocate_size, nullptr, GL_STREAM_DRAW);
    }
}

OGLStreamBuffer::~OGLStreamBuffer() {
    if (persistent) {
        glBindBuffer(gl_target, gl_buffer.handle);
        glUnmapBuffer(gl_target);
    }
    gl_buffer.Release();
}

GLuint OGLStreamBuffer::GetHandle() const {
    return gl_buffer.handle;
}

GLsizeiptr OGLStreamBuffer::GetSize() const {
    return buffer_size;
}

std::tuple<u8*, GLintptr, bool> OGLStreamBuffer::Map(GLsizeiptr size, GLintptr alignment) {
    ASSERT_MSG(size <= buffer_size, "Requested size {} exceeds buffer size {}", size, buffer_size);
    ASSERT(alignment <= buffer_size);
    mapped_size = size;

    if (alignment > 0) {
        buffer_pos = Common::AlignUp<std::size_t>(buffer_pos, alignment);
    }

    bool invalidate = false;
    if (buffer_pos + size > buffer_size) {
        buffer_pos = 0;
        invalidate = true;

        if (persistent) {
//gvx64            while (glGetError() != 0 ) //gvx64
//gvx64                continue; //flush the glGetError stack gvx64
            glUnmapBuffer(gl_target);
//gvx64            unsigned int error = glGetError();
//gvx64            if (error != 0 )
//gvx64                printf("../src/video_core/renderer_opengl/gl_stream_buffer.cpp,  OGLStreamBuffer::Map(GLsizeiptr size, GLintptr alignment),glUnmapBuffer(gl_target);, error code = %x\n",error); //gvx64
        }
    }

    if (invalidate || !persistent) {
        BORKED3DS_PROFILE("OpenGL", "Stream Buffer Orphaning");
        GLbitfield flags = GL_MAP_WRITE_BIT | (persistent ? GL_MAP_PERSISTENT_BIT : 0) |
                           (coherent ? GL_MAP_COHERENT_BIT : GL_MAP_FLUSH_EXPLICIT_BIT) |
                           (invalidate ? GL_MAP_INVALIDATE_BUFFER_BIT : GL_MAP_UNSYNCHRONIZED_BIT);
//gvx64        while (glGetError() != 0 ) //gvx64
//gvx64            continue; //flush the glGetError stack gvx64
        mapped_ptr = static_cast<u8*>(
            glMapBufferRange(gl_target, buffer_pos, buffer_size - buffer_pos, flags));
//gvx64        unsigned int error = glGetError();
//gvx64        if (error != 0)
//gvx64            printf("../src/video_core/renderer_opengl/gl_stream_buffer.cpp,  OGLStreamBuffer::Map(GLsizeiptr size, GLintptr alignment),glMapBufferRange(gl_target, buffer_pos, buffer_size - buffer_pos, flags));, error code = %x\n",error); //gvx64
        mapped_offset = buffer_pos;
    }

    return std::make_tuple(mapped_ptr + buffer_pos - mapped_offset, buffer_pos, invalidate);
}

void OGLStreamBuffer::Unmap(GLsizeiptr size) {
    ASSERT(size <= mapped_size);

    if (!coherent && size > 0) {
//gvx64        while (glGetError() != 0 ) //gvx64
//gvx64            continue; //flush the glGetError stack gvx64
        glFlushMappedBufferRange(gl_target, buffer_pos - mapped_offset, size);
//gvx64        unsigned int error = glGetError();
//gvx64        if (error != 0)
//gvx64            printf("../src/video_core/renderer_opengl/gl_stream_buffer.cpp, OGLStreamBuffer::Unmap(GLsizeiptr size), glFlushMappedBufferRange(gl_target, buffer_pos - mapped_offset, size);, error code = %x\n",error); //gvx64
    }

    if (!persistent) {
//gvx64        while (glGetError() != 0 ) //gvx64
//gvx64            continue; //flush the glGetError stack gvx64
        glUnmapBuffer(gl_target);
//gvx64        unsigned int error = glGetError();
//gvx64        if (error != 0 )
//gvx64            printf("../src/video_core/renderer_opengl/gl_stream_buffer.cpp, OGLStreamBuffer::Unmap(GLsizeiptr size),glUnmapBuffer(gl_target);, error code = %x\n",error); //gvx64
    }

    buffer_pos += size;
}

} // namespace OpenGL
