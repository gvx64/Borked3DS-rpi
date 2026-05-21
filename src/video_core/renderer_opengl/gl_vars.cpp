// Copyright 2019 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/renderer_opengl/gl_vars.h"

namespace OpenGL {
bool GLES;
std::atomic<bool> g_emergency_sw_active{false}; // gvx64
}
