// Copyright 2019 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once
#include <atomic>

namespace OpenGL {
extern bool GLES;
// gvx64: set true after Emergency SW Fallback RAM savestate load.
// Blocks game shader compilation in LoadProgram without affecting
// presentation shaders (gl_blit_helper) which keep screens visible.
extern std::atomic<bool> g_emergency_sw_active;
}
