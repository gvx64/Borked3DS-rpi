// Copyright 2023 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>

#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {

/**
 * @brief Optimizes SPIR-V code
 * @param code The string containing SPIR-V code
 */
std::vector<u32> OptimizeSPIRV(std::vector<u32> code);

/**
 * @brief Compiles GLSL into SPIRV
 * @param code The string containing GLSL code.
 * @param stage The pipeline stage the shader will be used in.
 * @param device The vulkan device handle.
 * @param premable Code that occurs before the main code block
 */
std::vector<u32> CompileGLSLtoSPIRV(std::string_view code, vk::ShaderStageFlagBits stage,
                                    vk::Device device, std::string_view premable = "");

/**
 * @brief Creates a vulkan shader module from GLSL by converting it to SPIR-V using glslang.
 * @param code The string containing GLSL code.
 * @param stage The pipeline stage the shader will be used in.
 * @param device The vulkan device handle.
 * @param premable Code that occurs before the main code block
 */
vk::ShaderModule Compile(std::string_view code, vk::ShaderStageFlagBits stage, vk::Device device,
                         std::string_view premable = "");

/**
 * @brief Creates a vulkan shader module from SPIR-V bytecode.
 * @param code The SPIR-V bytecode data.
 * @param device The vulkan device handle
 */
vk::ShaderModule CompileSPV(std::span<const u32> code, vk::Device device);

} // namespace Vulkan
