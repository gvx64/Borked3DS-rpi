// Copyright 2023 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

//? #version 430 core

#ifdef GL_ES
#define gl_VertexID gl_VertexIndex
#endif

layout(location = 0) out vec2 dst_coord;

layout(location = 0) uniform mediump ivec2 dst_size;

#ifdef VULKAN
#define gl_VertexID gl_VertexIndex
#endif

const vec2 vertices[4] =
vec2[4](vec2(-1.0f, -1.0f), vec2(1.0f, -1.0f), vec2(-1.0f, 1.0f), vec2(1.0f, 1.0f));

void main() {
    gl_Position = vec4(vertices[gl_VertexID], 0.0f, 1.0f);
    dst_coord = (vertices[gl_VertexID] / 2.0f + 0.5f) * vec2(dst_size);
}
