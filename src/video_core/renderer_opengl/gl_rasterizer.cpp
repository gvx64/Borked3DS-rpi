// Copyright 2022 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Copyright 2025 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/gl.h>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/literals.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/profiling.h"
#include "common/settings.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_vars.h"
#include "video_core/renderer_opengl/pica_to_gl.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/texture/texture_decode.h"

namespace OpenGL {

namespace {

using VideoCore::SurfaceType;
using namespace Common::Literals;
using namespace Pica::Shader::Generator;

constexpr std::size_t VERTEX_BUFFER_SIZE = 16_MiB;
constexpr std::size_t INDEX_BUFFER_SIZE = 2_MiB;
constexpr std::size_t UNIFORM_BUFFER_SIZE = 2_MiB;
constexpr std::size_t TEXTURE_BUFFER_SIZE = 2_MiB;

GLenum MakePrimitiveMode(Pica::PipelineRegs::TriangleTopology topology) {
    switch (topology) {
    case Pica::PipelineRegs::TriangleTopology::Shader:
    case Pica::PipelineRegs::TriangleTopology::List:
        return GL_TRIANGLES;
    case Pica::PipelineRegs::TriangleTopology::Fan:
        return GL_TRIANGLE_FAN;
    case Pica::PipelineRegs::TriangleTopology::Strip:
        return GL_TRIANGLE_STRIP;
    default:
        UNREACHABLE();
    }
    return GL_TRIANGLES;
}

GLenum MakeAttributeType(Pica::PipelineRegs::VertexAttributeFormat format) {
    switch (format) {
    case Pica::PipelineRegs::VertexAttributeFormat::BYTE:
        return GL_BYTE;
    case Pica::PipelineRegs::VertexAttributeFormat::UBYTE:
        return GL_UNSIGNED_BYTE;
    case Pica::PipelineRegs::VertexAttributeFormat::SHORT:
        return GL_SHORT;
    case Pica::PipelineRegs::VertexAttributeFormat::FLOAT:
        return GL_FLOAT;
    }
    return GL_UNSIGNED_BYTE;
}

[[nodiscard]] GLsizeiptr TextureBufferSize(const Driver& driver, bool is_lf) {
    const bool is_gles = driver.IsOpenGLES();
    if (is_gles) {
        GLint max_size;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_size);

        // Use the minimum of the maximum available size and our desired size
        if (is_lf) {
//gvx64            printf("../src/video_core/renderer_opengl/gl_rasterizer.cpp, TextureBufferSize(const Driver& driver, bool is_lf), max_size = %08x\n", max_size); //gvx64
            return std::min<GLsizeiptr>(max_size, 64 * 1024);
        }
        return std::min<GLsizeiptr>(max_size, 32 * 1024);
    }

    // Use the smallest texel size from the texel views
    // which corresponds to GL_RG32F
    GLint max_texel_buffer_size;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texel_buffer_size);
    GLsizeiptr candidate = std::min<GLsizeiptr>(max_texel_buffer_size * 8ULL, TEXTURE_BUFFER_SIZE);

    if (driver.HasBug(DriverBug::SlowTextureBufferWithBigSize) && !is_lf) {
        constexpr GLsizeiptr FIXUP_TEXTURE_BUFFER_SIZE = static_cast<GLsizeiptr>(1 << 14); // 16384
        return FIXUP_TEXTURE_BUFFER_SIZE;
    }

    return candidate;
}

} // Anonymous namespace

RasterizerOpenGL::RasterizerOpenGL(Memory::MemorySystem& memory, Pica::PicaCore& pica,
                                   VideoCore::CustomTexManager& custom_tex_manager,
                                   VideoCore::RendererBase& renderer, Driver& driver_)
    : VideoCore::RasterizerAccelerated{memory, pica}, driver{driver_},
      shader_manager{renderer.GetRenderWindow(), driver, !driver.IsOpenGLES()},
      runtime{driver, renderer}, res_cache{memory, custom_tex_manager, runtime, regs, renderer},
      vertex_buffer{driver, GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(driver.IsOpenGLES() ? VERTEX_BUFFER_SIZE / 2
                                                                : VERTEX_BUFFER_SIZE)},
      uniform_buffer{driver, GL_UNIFORM_BUFFER,
                     static_cast<GLsizeiptr>(driver.IsOpenGLES() ? UNIFORM_BUFFER_SIZE / 2
                                                                 : UNIFORM_BUFFER_SIZE)},
      index_buffer{
          driver, GL_ELEMENT_ARRAY_BUFFER,
          static_cast<GLsizeiptr>(driver.IsOpenGLES() ? INDEX_BUFFER_SIZE / 2 : INDEX_BUFFER_SIZE)},
      texture_buffer{driver, GL_TEXTURE_BUFFER_OES, TextureBufferSize(driver, false)},
      texture_lf_buffer{driver, GL_TEXTURE_BUFFER_OES, TextureBufferSize(driver, true)} {
    const bool is_gles = driver.IsOpenGLES();
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    // Check required GLES extensions
    if (is_gles) {
        if (!driver.HasExtension("GL_OES_vertex_array_object")) {
            LOG_CRITICAL(Render_OpenGL, "GL_OES_vertex_array_object is required!");
            throw std::runtime_error("Missing required OpenGL ES extension");
        }
    }

    // Clipping plane 0 is always enabled for PICA fixed clip plane z <= 0
    state.clip_distance[0] = true;

    // Generate VAO
//gvx64    sw_vao.Create();
//gvx64    hw_vao.Create();
    if (OpenGL::g_use_vao){ // gvx64 only use vao in qt5 binary
        sw_vao.Create(); //gvx64
        hw_vao.Create(); //gxv64
    } else { //gvx64
        // In CLI mode we will skip vao creation due to multiple competing vao context issues on Raspberry Pi
        // We will use glVertexAttribPointer directly instead
    } //gvx64


    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &uniform_buffer_alignment);
    uniform_size_aligned_vs_pica =
        Common::AlignUp<std::size_t>(sizeof(VSPicaUniformData), uniform_buffer_alignment);
    uniform_size_aligned_vs =
        Common::AlignUp<std::size_t>(sizeof(VSUniformData), uniform_buffer_alignment);
    uniform_size_aligned_fs =
        Common::AlignUp<std::size_t>(sizeof(FSUniformData), uniform_buffer_alignment);

    // Set vertex attributes for software shader path
    state.draw.vertex_array = sw_vao.handle;
    state.draw.vertex_buffer = vertex_buffer.GetHandle();
    state.Apply();

    SetupHardwareVertexAttribPointers(); //gvx64

    // Allocate and bind texture buffer lut textures
    texture_buffer_lut_lf.Create();
    texture_buffer_lut_rg.Create();
    texture_buffer_lut_rgba.Create();
    state.texture_buffer_lut_lf.texture_buffer = texture_buffer_lut_lf.handle;
    state.texture_buffer_lut_rg.texture_buffer = texture_buffer_lut_rg.handle;
    state.texture_buffer_lut_rgba.texture_buffer = texture_buffer_lut_rgba.handle;
    state.Apply();
    glActiveTexture(TextureUnits::TextureBufferLUT_LF.Enum());

    if (is_gles && majorVersion == 3 && minorVersion < 2) {
/*        // Check for GL_EXT_texture_buffer support
        if (GLAD_GL_EXT_texture_buffer) {
            // Use floating-point formats if supported
            glTexBufferEXT(GL_TEXTURE_BUFFER, GL_RG32F, texture_lf_buffer.GetHandle());
            glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
            glTexBufferEXT(GL_TEXTURE_BUFFER, GL_RG32F, texture_buffer.GetHandle());
            glActiveTexture(TextureUnits::TextureBufferLUT_RGBA.Enum());
            glTexBufferEXT(GL_TEXTURE_BUFFER, GL_RGBA32F, texture_buffer.GetHandle());*/
        // Check for GL_OES_texture_buffer support
        if (GLAD_GL_OES_texture_buffer) {
            // Primary: Use texture buffer where available
            LOG_INFO(Render_OpenGL, "GL_OES_texture_buffer is available, utilizing "
                                    "primary code path");
            // Use floating-point formats if supported
            glBindTexture(GL_TEXTURE_BUFFER_OES, texture_buffer_lut_lf.handle);
            glTexBufferOES(GL_TEXTURE_BUFFER_OES, GL_RG32F, texture_lf_buffer.GetHandle());
            glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
            glBindTexture(GL_TEXTURE_BUFFER_OES, texture_buffer_lut_rg.handle);
            glTexBufferOES(GL_TEXTURE_BUFFER_OES, GL_RG32F, texture_buffer.GetHandle());
            glActiveTexture(TextureUnits::TextureBufferLUT_RGBA.Enum());
            glBindTexture(GL_TEXTURE_BUFFER_OES, texture_buffer_lut_rgba.handle);
            glTexBufferOES(GL_TEXTURE_BUFFER_OES, GL_RGBA32F, texture_buffer.GetHandle());
            fs_uniform_block_data.data.use_texture2d_lut = 0;
            using_texture2d_lut = false;
        } else {
            // Fallback: Use 1D textures emulated as 2D textures for GLES
            LOG_INFO(Render_OpenGL, "GL_EXT_texture_buffer not available, falling "
                                    "back to 2D texture LUTs");

            using_texture2d_lut = true;

            // Setup LF buffer texture (256 entries)
            glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_lf.handle);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, 256, 1, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Setup RG buffer texture
            glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
            glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rg.handle);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, 256, 1, 0, GL_RG, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Setup RGBA buffer texture
            glActiveTexture(TextureUnits::TextureBufferLUT_RGBA.Enum());
            glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rgba.handle);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Lighting LUT texture: 1024x2 for 8 LUTs, 256 elements each
            glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rg.handle);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, 1024, 2, 0, GL_RG, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Fog LUT texture: 128x1
            glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_lf.handle);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, 128, 1, 0, GL_RG, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Set flag for shader
            fs_uniform_block_data.data.use_texture2d_lut = 1;
            fs_uniform_block_data.dirty = true;
        }
    } else {
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, texture_lf_buffer.GetHandle());
        glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, texture_buffer.GetHandle());
        glActiveTexture(TextureUnits::TextureBufferLUT_RGBA.Enum());
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, texture_buffer.GetHandle());
    }

    // Bind index buffer for hardware shader path
    state.draw.vertex_array = hw_vao.handle;
    state.Apply();
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer.GetHandle());

    glEnable(GL_BLEND);

    SyncEntireState();
    OpenGLState::rasterizer_ptr = this; //gvx64 - added to provide access to SetupHardwareVertexAttribPointers() in gl_state.cpp
}

RasterizerOpenGL::~RasterizerOpenGL() = default;

void RasterizerOpenGL::TickFrame() {
    res_cache.TickFrame();
}

void RasterizerOpenGL::LoadDiskResources(const std::atomic_bool& stop_loading,
                                         const VideoCore::DiskResourceLoadCallback& callback) {
    shader_manager.LoadDiskCache(stop_loading, callback, accurate_mul);
}

void RasterizerOpenGL::SyncFixedState() {
    SyncClipEnabled();
    SyncCullMode();
    SyncBlendEnabled();
    SyncBlendFuncs();
    SyncBlendColor();
    SyncLogicOp();
    SyncStencilTest();
    SyncDepthTest();
    SyncColorWriteMask();
    SyncStencilWriteMask();
    SyncDepthWriteMask();
}

void RasterizerOpenGL::SetupHardwareVertexAttribPointers() {
    glVertexAttribPointer(ATTRIBUTE_POSITION, 4, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, position));
    glEnableVertexAttribArray(ATTRIBUTE_POSITION);

    glVertexAttribPointer(ATTRIBUTE_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, color));
    glEnableVertexAttribArray(ATTRIBUTE_COLOR);

    glVertexAttribPointer(ATTRIBUTE_TEXCOORD0, 2, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, tex_coord0));
    glEnableVertexAttribArray(ATTRIBUTE_TEXCOORD0);

    glVertexAttribPointer(ATTRIBUTE_TEXCOORD1, 2, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, tex_coord1));
    glEnableVertexAttribArray(ATTRIBUTE_TEXCOORD1);

    glVertexAttribPointer(ATTRIBUTE_TEXCOORD2, 2, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, tex_coord2));
    glEnableVertexAttribArray(ATTRIBUTE_TEXCOORD2);

    glVertexAttribPointer(ATTRIBUTE_TEXCOORD0_W, 1, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, tex_coord0_w));
    glEnableVertexAttribArray(ATTRIBUTE_TEXCOORD0_W);

    glVertexAttribPointer(ATTRIBUTE_NORMQUAT, 4, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, normquat));
    glEnableVertexAttribArray(ATTRIBUTE_NORMQUAT);

    glVertexAttribPointer(ATTRIBUTE_VIEW, 3, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex),
                          (GLvoid*)offsetof(HardwareVertex, view));
    glEnableVertexAttribArray(ATTRIBUTE_VIEW);
}

void RasterizerOpenGL::SetupVertexArray(u8* array_ptr, GLintptr buffer_offset,
                                        GLuint vs_input_index_min, GLuint vs_input_index_max) {
    GLint majorVersion = 0, minorVersion = 0; //gvx64
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion); //gvx64
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion); //gvx64
    if (OpenGL::g_use_vao) { //gvx64
        // CLI mode -  fallback path - no VAO usage to avoid competing vao context issues on raspberry pi
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer.GetHandle()); //gvx64
        SetupHardwareVertexAttribPointers(); //gvx64
        return; //gvx64
    } //gvx64
    BORKED3DS_PROFILE("OpenGL", "Vertex Array Setup");
    const auto& vertex_attributes = regs.pipeline.vertex_attributes;
    PAddr base_address = vertex_attributes.GetPhysicalBaseAddress();

    state.draw.vertex_array = hw_vao.handle;
    state.draw.vertex_buffer = vertex_buffer.GetHandle();
    state.Apply();

    std::array<bool, 16> enable_attributes{};

    for (const auto& loader : vertex_attributes.attribute_loaders) {
        if (loader.component_count == 0 || loader.byte_count == 0) {
            continue;
        }

        u32 offset = 0;
        for (u32 comp = 0; comp < loader.component_count && comp < 12; ++comp) {
            u32 attribute_index = loader.GetComponent(comp);
            if (attribute_index < 12) {
                if (vertex_attributes.GetNumElements(attribute_index) != 0) {
                    offset = Common::AlignUp(
                        offset, vertex_attributes.GetElementSizeInBytes(attribute_index));

                    u32 input_reg = regs.vs.GetRegisterForAttribute(attribute_index);
                    GLint size = vertex_attributes.GetNumElements(attribute_index);
                    GLenum type = MakeAttributeType(vertex_attributes.GetFormat(attribute_index));
                    GLsizei stride = loader.byte_count;
                    glVertexAttribPointer(input_reg, size, type, GL_FALSE, stride,
                                          reinterpret_cast<GLvoid*>(buffer_offset + offset));
                    enable_attributes[input_reg] = true;

                    offset += vertex_attributes.GetStride(attribute_index);
                }
            } else {
                // Attribute ids 12, 13, 14 and 15 signify 4, 8, 12 and 16-byte
                // paddings, respectively
                offset = Common::AlignUp(offset, 4);
                offset += (attribute_index - 11) * 4;
            }
        }

        const PAddr data_addr =
            base_address + loader.data_offset + (vs_input_index_min * loader.byte_count);

        const u32 vertex_num = vs_input_index_max - vs_input_index_min + 1;
        const u32 data_size = loader.byte_count * vertex_num;

        res_cache.FlushRegion(data_addr, data_size);
        std::memcpy(array_ptr, memory.GetPhysicalPointer(data_addr), data_size);

        array_ptr += data_size;
        buffer_offset += data_size;
    }

    for (std::size_t i = 0; i < enable_attributes.size(); ++i) {
        if (enable_attributes[i] != hw_vao_enabled_attributes[i]) {
            if (enable_attributes[i]) {
                glEnableVertexAttribArray(static_cast<GLuint>(i));
            } else {
                glDisableVertexAttribArray(static_cast<GLuint>(i));
            }
            hw_vao_enabled_attributes[i] = enable_attributes[i];
        }

        if (vertex_attributes.IsDefaultAttribute(i)) {
            const u32 reg = regs.vs.GetRegisterForAttribute(i);
            if (!enable_attributes[reg]) {
                const auto& attr = pica.input_default_attributes[i];
                glVertexAttrib4f(reg, attr.x.ToFloat32(), attr.y.ToFloat32(), attr.z.ToFloat32(),
                                 attr.w.ToFloat32());
            }
        }
    }
}

bool RasterizerOpenGL::SetupVertexShader() {
    BORKED3DS_PROFILE("OpenGL", "Vertex Shader Setup");
    return shader_manager.UseProgrammableVertexShader(regs, pica.vs_setup, accurate_mul);
}

bool RasterizerOpenGL::SetupGeometryShader() {
    BORKED3DS_PROFILE("OpenGL", "Geometry Shader Setup");

    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

//gvx64    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
//gvx64        LOG_DEBUG(Render_OpenGL,
//gvx64                  "Accelerate draw under OpenGLES < 3.2 doesn't support geometry shader");
//gvx64        return false;
//gvx64    }

    if (regs.pipeline.use_gs != Pica::PipelineRegs::UseGS::No) {
        LOG_ERROR(Render_OpenGL, "Accelerate draw doesn't support geometry shader");
        return false;
    }

    // Enable the quaternion fix-up geometry-shader only if we are actually doing
    // per-fragment lighting and care about proper quaternions. Otherwise just use
    // standard vertex+fragment shaders
    if (regs.lighting.disable) {
        shader_manager.UseTrivialGeometryShader();
    } else {
        shader_manager.UseFixedGeometryShader(regs);
    }

    return true;
}

bool RasterizerOpenGL::AccelerateDrawBatch(bool is_indexed) {
    if (regs.pipeline.use_gs != Pica::PipelineRegs::UseGS::No) {
        if (regs.pipeline.gs_config.mode != Pica::PipelineRegs::GSMode::Point) {
            return false;
        }
        if (regs.pipeline.triangle_topology != Pica::PipelineRegs::TriangleTopology::Shader) {
            return false;
        }
    }

    if (!SetupVertexShader()) {
        return false;
    }

    if (!SetupGeometryShader()) {
        return false;
    }

    return Draw(true, is_indexed);
}

bool RasterizerOpenGL::AccelerateDrawBatchInternal(bool is_indexed) {
    const GLenum primitive_mode = MakePrimitiveMode(regs.pipeline.triangle_topology);
    auto [vs_input_index_min, vs_input_index_max, vs_input_size] = AnalyzeVertexArray(is_indexed);

    const bool is_gles = driver.IsOpenGLES();
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (vs_input_size > VERTEX_BUFFER_SIZE) {
        LOG_WARNING(Render_OpenGL, "Too large vertex input size {}", vs_input_size);
        return false;
    }

    state.draw.vertex_buffer = vertex_buffer.GetHandle();
    state.Apply();

    u8* buffer_ptr;
    GLintptr buffer_offset;
    std::tie(buffer_ptr, buffer_offset, std::ignore) = vertex_buffer.Map(vs_input_size, 4);
    LOG_DEBUG(Render_OpenGL, "Index buffer offset: {}", buffer_offset);
    SetupVertexArray(buffer_ptr, buffer_offset, vs_input_index_min, vs_input_index_max);
    vertex_buffer.Unmap(vs_input_size);

    shader_manager.ApplyTo(state, accurate_mul);
    state.Apply();

/*    if (is_indexed) {
        bool index_u16 = regs.pipeline.index_array.format != 0;
        std::size_t index_buffer_size = regs.pipeline.num_vertices * (index_u16 ? 2 : 1);
        if (index_buffer_size > INDEX_BUFFER_SIZE) {
            LOG_WARNING(Render_OpenGL, "Too large index input size {}", index_buffer_size);
            return false;
        }

        const u8* index_data =
            memory.GetPhysicalPointer(regs.pipeline.vertex_attributes.GetPhysicalBaseAddress() +
                                      regs.pipeline.index_array.offset);
        if (index_u16) {
            const u16* indices = reinterpret_cast<const u16*>(index_data);
            u16 min_idx = *std::min_element(indices, indices + regs.pipeline.num_vertices);
            u16 max_idx = *std::max_element(indices, indices + regs.pipeline.num_vertices);
            LOG_DEBUG(Render_OpenGL, "Index range: {} to {}, vs_input_index_min: {}", min_idx,
                      max_idx, vs_input_index_min);
        }
        std::tie(buffer_ptr, buffer_offset, std::ignore) = index_buffer.Map(index_buffer_size, 4);
        LOG_DEBUG(Render_OpenGL, "Index buffer offset: {}", buffer_offset);
        std::memcpy(buffer_ptr, index_data, index_buffer_size);
        index_buffer.Unmap(index_buffer_size);

        // Ensure vs_input_index_min is not zero before applying the negative offset
        if (vs_input_index_min < 0 || vs_input_index_min > vs_input_index_max) {
            LOG_ERROR(Render_OpenGL, "Invalid vertex index range");
            return false;
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer.GetHandle());

        LOG_DEBUG(Render_OpenGL, "Drawing: mode={}, count={}, type={}, offset={}, basevertex=0",
                  primitive_mode, regs.pipeline.num_vertices,
                  index_u16 ? "GL_UNSIGNED_SHORT" : "GL_UNSIGNED_BYTE", buffer_offset);

        // Use extension if available, otherwise fallback
        if ((is_gles && majorVersion == 3 && minorVersion < 2) ||
            !GLAD_GL_OES_draw_elements_base_vertex) {
            // Adjust indices by subtracting vs_input_index_min
            // Assuming index data is accessible and modifiable:
            GLenum type = index_u16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
            GLsizei count = regs.pipeline.num_vertices;
            const GLvoid* indices = reinterpret_cast<const void*>(buffer_offset);

            // Map the index buffer to modify indices
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer.GetHandle());
            void* mapped = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, buffer_offset,
                                            count * (type == GL_UNSIGNED_SHORT ? 2 : 1),
                                            GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
            if (mapped) {
                if (type == GL_UNSIGNED_SHORT) {
                    uint16_t* idx = static_cast<uint16_t*>(mapped);
                    for (GLsizei i = 0; i < count; ++i) {
                        idx[i] -= vs_input_index_min;
                    }
                } else {
                    uint8_t* idx = static_cast<uint8_t*>(mapped);
                    for (GLsizei i = 0; i < count; ++i) {
                        idx[i] -= vs_input_index_min;
                    }
                }
                glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
            }

            // Draw with adjusted indices
            glDrawElements(primitive_mode, count, type, indices);
        } else {
            glDrawRangeElementsBaseVertex(primitive_mode, vs_input_index_min, vs_input_index_max,
                                          regs.pipeline.num_vertices,
                                          index_u16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE,
                                          reinterpret_cast<const void*>(buffer_offset),
                                          -static_cast<GLint>(vs_input_index_min));
        }
    } */
if (is_indexed) { //gvx64 begin
    bool index_u16 = regs.pipeline.index_array.format != 0;
    std::size_t index_buffer_size = regs.pipeline.num_vertices * (index_u16 ? 2 : 1);
    if (index_buffer_size > INDEX_BUFFER_SIZE) {
        LOG_WARNING(Render_OpenGL, "Too large index input size {}", index_buffer_size);
        return false;
    }

    const u8* index_data =
        memory.GetPhysicalPointer(regs.pipeline.vertex_attributes.GetPhysicalBaseAddress() +
                                  regs.pipeline.index_array.offset);
    if (index_u16) {
        const u16* indices = reinterpret_cast<const u16*>(index_data);
        u16 min_idx = *std::min_element(indices, indices + regs.pipeline.num_vertices);
        u16 max_idx = *std::max_element(indices, indices + regs.pipeline.num_vertices);
        LOG_DEBUG(Render_OpenGL, "Index range: {} to {}, vs_input_index_min: {}", min_idx,
                  max_idx, vs_input_index_min);
    }

    // Upload index data using glBufferSubData instead of glMapBufferRange
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer.GetHandle());
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_buffer_size, index_data);

    if (vs_input_index_min < 0 || vs_input_index_min > vs_input_index_max) {
        LOG_ERROR(Render_OpenGL, "Invalid vertex index range");
        return false;
    }

    GLenum type = index_u16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    const void* indices = reinterpret_cast<const void*>(0); // offset into the buffer

    if ((is_gles && majorVersion == 3 && minorVersion < 2) ||
        !GLAD_GL_OES_draw_elements_base_vertex) {
        // Adjust index values manually
        std::vector<u8> adjusted_indices(index_buffer_size);
        if (index_u16) {
            const u16* src = reinterpret_cast<const u16*>(index_data);
            u16* dst = reinterpret_cast<u16*>(adjusted_indices.data());
            for (size_t i = 0; i < regs.pipeline.num_vertices; ++i) {
                dst[i] = src[i] - vs_input_index_min;
            }
        } else {
            const u8* src = index_data;
            u8* dst = adjusted_indices.data();
            for (size_t i = 0; i < regs.pipeline.num_vertices; ++i) {
                dst[i] = src[i] - vs_input_index_min;
            }
        }
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_buffer_size, adjusted_indices.data());
        glDrawElements(primitive_mode, regs.pipeline.num_vertices, type, indices);
    } else {
        glDrawRangeElementsBaseVertex(primitive_mode, vs_input_index_min, vs_input_index_max,
                                      regs.pipeline.num_vertices, type, indices,
                                      -static_cast<GLint>(vs_input_index_min));
    }
} //gvx64 end
    else {
        glDrawArrays(primitive_mode, 0, regs.pipeline.num_vertices);
    }
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        LOG_ERROR(Render_OpenGL, "OpenGL error before draw: {}", error);
        return false;
    }
    return true;
}

void RasterizerOpenGL::DrawTriangles() {
    if (vertex_batch.empty())
        return;
    Draw(false, false);
}

bool RasterizerOpenGL::Draw(bool accelerate, bool is_indexed) {

    BORKED3DS_PROFILE("OpenGL", "Drawing");
    const bool shadow_rendering = regs.framebuffer.IsShadowRendering();
    const bool has_stencil = regs.framebuffer.HasStencil();

    const bool write_color_fb = shadow_rendering || state.color_mask.red_enabled == GL_TRUE ||
                                state.color_mask.green_enabled == GL_TRUE ||
                                state.color_mask.blue_enabled == GL_TRUE ||
                                state.color_mask.alpha_enabled == GL_TRUE;
    const bool write_depth_fb =
        (state.depth.test_enabled && state.depth.write_mask == GL_TRUE) ||
        (has_stencil && state.stencil.test_enabled && state.stencil.write_mask != 0);

    const bool using_color_fb =
        regs.framebuffer.framebuffer.GetColorBufferPhysicalAddress() != 0 && write_color_fb;
    const bool using_depth_fb =
        !shadow_rendering && regs.framebuffer.framebuffer.GetDepthBufferPhysicalAddress() != 0 &&
        (write_depth_fb || regs.framebuffer.output_merger.depth_test_enable != 0 ||
         (has_stencil && state.stencil.test_enabled));

    const auto fb_helper = res_cache.GetFramebufferSurfaces(using_color_fb, using_depth_fb);
    const Framebuffer* framebuffer = fb_helper.Framebuffer();
    if (!framebuffer->color_id && framebuffer->shadow_rendering) {
        return true;
    }

    // Bind the framebuffer surfaces
    if (shadow_rendering) {
        state.image_shadow_buffer = framebuffer->Attachment(SurfaceType::Color);
    }
    state.draw.draw_framebuffer = framebuffer->Handle();

    // Sync the viewport
    const auto viewport = fb_helper.Viewport();
    state.viewport.x = static_cast<GLint>(viewport.x);
    state.viewport.y = static_cast<GLint>(viewport.y);
    state.viewport.width = static_cast<GLsizei>(viewport.width);
    state.viewport.height = static_cast<GLsizei>(viewport.height);

    // Viewport can have negative offsets or larger dimensions than our framebuffer sub-rect.
    // Enable scissor test to prevent drawing outside of the framebuffer region
    const auto draw_rect = fb_helper.DrawRect();
    state.scissor.enabled = true;
    state.scissor.x = draw_rect.left;
    state.scissor.y = draw_rect.bottom;
    state.scissor.width = draw_rect.GetWidth();
    state.scissor.height = draw_rect.GetHeight();

    // Update scissor uniforms
    const auto [scissor_x1, scissor_y2, scissor_x2, scissor_y1] = fb_helper.Scissor();
    if (fs_uniform_block_data.data.scissor_x1 != scissor_x1 ||
        fs_uniform_block_data.data.scissor_x2 != scissor_x2 ||
        fs_uniform_block_data.data.scissor_y1 != scissor_y1 ||
        fs_uniform_block_data.data.scissor_y2 != scissor_y2) {

        fs_uniform_block_data.data.scissor_x1 = scissor_x1;
        fs_uniform_block_data.data.scissor_x2 = scissor_x2;
        fs_uniform_block_data.data.scissor_y1 = scissor_y1;
        fs_uniform_block_data.data.scissor_y2 = scissor_y2;
        fs_uniform_block_data.dirty = true;
    }

    // Sync and bind the texture surfaces
    SyncTextureUnits(framebuffer);
    state.Apply();

    // Sync and bind the shader
    if (shader_dirty) {
        shader_manager.UseFragmentShader(regs, user_config);
        shader_dirty = false;
    }

    // Sync the LUTs within the texture buffer
    SyncAndUploadLUTs();
    SyncAndUploadLUTsLF();

    // Sync the uniform data
    UploadUniforms(accelerate);

    // Draw the vertex batch
    bool succeeded = true;
    if (accelerate) {
        succeeded = AccelerateDrawBatchInternal(is_indexed);
    } else {
        state.draw.vertex_array = sw_vao.handle;
        state.draw.vertex_buffer = vertex_buffer.GetHandle();
        shader_manager.UseTrivialVertexShader();
        shader_manager.UseTrivialGeometryShader();
        shader_manager.ApplyTo(state, accurate_mul);
        state.Apply();

        std::size_t max_vertices = 3 * (VERTEX_BUFFER_SIZE / (3 * sizeof(HardwareVertex)));
        for (std::size_t base_vertex = 0; base_vertex < vertex_batch.size();
             base_vertex += max_vertices) {
            const std::size_t vertices = std::min(max_vertices, vertex_batch.size() - base_vertex);
            const std::size_t vertex_size = vertices * sizeof(HardwareVertex);

            const auto [vbo, offset, _] = vertex_buffer.Map(vertex_size, sizeof(HardwareVertex));
            std::memcpy(vbo, vertex_batch.data() + base_vertex, vertex_size);
            vertex_buffer.Unmap(vertex_size);

            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(offset / sizeof(HardwareVertex)),
                         static_cast<GLsizei>(vertices));
        }
    }

    vertex_batch.clear();

    if (shadow_rendering) {
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    }

    return succeeded;
}

void RasterizerOpenGL::SyncTextureUnits(const Framebuffer* framebuffer) {
    using TextureType = Pica::TexturingRegs::TextureConfig::TextureType;

    // Reset transient draw state
    state.color_buffer.texture_2d = 0;
    user_config = {};

    const auto pica_textures = regs.texturing.GetTextures();
    for (u32 texture_index = 0; texture_index < pica_textures.size(); ++texture_index) {
        const auto& texture = pica_textures[texture_index];

        // If the texture unit is disabled unbind the corresponding gl unit
        if (!texture.enabled) {
            const Surface& null_surface = res_cache.GetSurface(VideoCore::NULL_SURFACE_ID);
            state.texture_units[texture_index].texture_2d = null_surface.Handle();
            continue;
        }

        // Handle special tex0 configurations
        if (texture_index == 0) {
            switch (texture.config.type.Value()) {
            case TextureType::Shadow2D: {
                Surface& surface = res_cache.GetTextureSurface(texture);
                surface.flags |= VideoCore::SurfaceFlagBits::ShadowMap;
                state.image_shadow_texture_px = surface.Handle();
                continue;
            }
            case TextureType::ShadowCube: {
                BindShadowCube(texture);
                continue;
            }
            case TextureType::TextureCube: {
                BindTextureCube(texture);
                continue;
            }
            default:
                UnbindSpecial();
            }
        }

        // Sync texture unit sampler
        Sampler& sampler = res_cache.GetSampler(texture.config);
        state.texture_units[texture_index].sampler = sampler.Handle();

        // Bind the texture provided by the rasterizer cache
        Surface& surface = res_cache.GetTextureSurface(texture);
        if (!IsFeedbackLoop(texture_index, framebuffer, surface)) {
            BindMaterial(texture_index, surface);
            state.texture_units[texture_index].texture_2d = surface.Handle();
        }
    }

    if (emulate_minmax_blend && !driver.HasShaderFramebufferFetch()) {
        state.color_buffer.texture_2d = framebuffer->Attachment(SurfaceType::Color);
    }
}

void RasterizerOpenGL::BindShadowCube(const Pica::TexturingRegs::FullTextureConfig& texture) {
    using CubeFace = Pica::TexturingRegs::CubeFace;
    auto info = Pica::Texture::TextureInfo::FromPicaRegister(texture.config, texture.format);
    constexpr std::array faces = {
        CubeFace::PositiveX, CubeFace::NegativeX, CubeFace::PositiveY,
        CubeFace::NegativeY, CubeFace::PositiveZ, CubeFace::NegativeZ,
    };

    for (CubeFace face : faces) {
        const u32 binding = static_cast<u32>(face);
        info.physical_address = regs.texturing.GetCubePhysicalAddress(face);

        VideoCore::SurfaceId surface_id = res_cache.GetTextureSurface(info);
        Surface& surface = res_cache.GetSurface(surface_id);
        surface.flags |= VideoCore::SurfaceFlagBits::ShadowMap;
        state.image_shadow_texture[binding] = surface.Handle();
    }
}

void RasterizerOpenGL::BindTextureCube(const Pica::TexturingRegs::FullTextureConfig& texture) {
    using CubeFace = Pica::TexturingRegs::CubeFace;
    const VideoCore::TextureCubeConfig config = {
        .px = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveX),
        .nx = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeX),
        .py = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveY),
        .ny = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeY),
        .pz = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveZ),
        .nz = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeZ),
        .width = texture.config.width,
        .levels = texture.config.lod.max_level + 1,
        .format = texture.format,
    };

    Surface& surface = res_cache.GetTextureCube(config);
    Sampler& sampler = res_cache.GetSampler(texture.config);
    state.texture_units[0].target = GL_TEXTURE_CUBE_MAP;
    state.texture_units[0].texture_2d = surface.Handle();
    state.texture_units[0].sampler = sampler.Handle();
}

void RasterizerOpenGL::BindMaterial(u32 texture_index, Surface& surface) {
    if (!surface.IsCustom()) {
        return;
    }

    const GLuint sampler = state.texture_units[texture_index].sampler;
    if (surface.HasNormalMap()) {
        if (regs.lighting.disable) {
            LOG_WARNING(Render_OpenGL, "Custom normal map used but scene has no light enabled");
        }
        glActiveTexture(TextureUnits::TextureNormalMap.Enum());
        glBindTexture(GL_TEXTURE_2D, surface.Handle(2));
        glBindSampler(TextureUnits::TextureNormalMap.id, sampler);
        user_config.use_custom_normal.Assign(1);
    }
}

bool RasterizerOpenGL::IsFeedbackLoop(u32 texture_index, const Framebuffer* framebuffer,
                                      Surface& surface) {
    const GLuint color_attachment = framebuffer->Attachment(SurfaceType::Color);
    const bool is_feedback_loop = color_attachment == surface.Handle();
    if (!is_feedback_loop) {
        return false;
    }

    state.texture_units[texture_index].texture_2d = surface.CopyHandle();
    return true;
}

void RasterizerOpenGL::UnbindSpecial() {
    state.texture_units[0].texture_2d = 0;
    state.texture_units[0].target = GL_TEXTURE_2D;
    state.image_shadow_texture_px = 0;
    state.image_shadow_texture_nx = 0;
    state.image_shadow_texture_py = 0;
    state.image_shadow_texture_ny = 0;
    state.image_shadow_texture_pz = 0;
    state.image_shadow_texture_nz = 0;
    state.image_shadow_buffer = 0;
}

void RasterizerOpenGL::NotifyFixedFunctionPicaRegisterChanged(u32 id) {
    switch (id) {
    // Clipping plane
    case PICA_REG_INDEX(rasterizer.clip_enable):
        SyncClipEnabled();
        break;

    // Culling
    case PICA_REG_INDEX(rasterizer.cull_mode):
        SyncCullMode();
        break;

    // Blending
    case PICA_REG_INDEX(framebuffer.output_merger.alphablend_enable):
        SyncBlendEnabled();
        // Update since logic op emulation depends on alpha blend enable.
        SyncLogicOp();
        SyncColorWriteMask();
        break;
    case PICA_REG_INDEX(framebuffer.output_merger.alpha_blending):
        SyncBlendFuncs();
        break;
    case PICA_REG_INDEX(framebuffer.output_merger.blend_const):
        SyncBlendColor();
        break;

    // Sync GL stencil test + stencil write mask
    // (Pica stencil test function register also contains a stencil write mask)
    case PICA_REG_INDEX(framebuffer.output_merger.stencil_test.raw_func):
        SyncStencilTest();
        SyncStencilWriteMask();
        break;
    case PICA_REG_INDEX(framebuffer.output_merger.stencil_test.raw_op):
    case PICA_REG_INDEX(framebuffer.framebuffer.depth_format):
        SyncStencilTest();
        break;

    // Sync GL depth test + depth and color write mask
    // (Pica depth test function register also contains a depth and color write
    // mask)
    case PICA_REG_INDEX(framebuffer.output_merger.depth_test_enable):
        SyncDepthTest();
        SyncDepthWriteMask();
        SyncColorWriteMask();
        break;

    // Sync GL depth and stencil write mask
    // (This is a dedicated combined depth / stencil write-enable register)
    case PICA_REG_INDEX(framebuffer.framebuffer.allow_depth_stencil_write):
        SyncDepthWriteMask();
        SyncStencilWriteMask();
        break;

    // Sync GL color write mask
    // (This is a dedicated color write-enable register)
    case PICA_REG_INDEX(framebuffer.framebuffer.allow_color_write):
        SyncColorWriteMask();
        break;

    // Logic op
    case PICA_REG_INDEX(framebuffer.output_merger.logic_op):
        SyncLogicOp();
        // Update since color write mask is used to emulate no-op.
        SyncColorWriteMask();
        break;
    }
}

void RasterizerOpenGL::FlushAll() {
    res_cache.FlushAll();
}

void RasterizerOpenGL::FlushRegion(PAddr addr, u32 size) {
    res_cache.FlushRegion(addr, size);
}

void RasterizerOpenGL::InvalidateRegion(PAddr addr, u32 size) {
    res_cache.InvalidateRegion(addr, size);
}

void RasterizerOpenGL::FlushAndInvalidateRegion(PAddr addr, u32 size) {
    res_cache.FlushRegion(addr, size);
    res_cache.InvalidateRegion(addr, size);
}

void RasterizerOpenGL::ClearAll(bool flush) {
    res_cache.ClearAll(flush);
}

bool RasterizerOpenGL::AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) {
    return res_cache.AccelerateDisplayTransfer(config);
}

bool RasterizerOpenGL::AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) {
    return res_cache.AccelerateTextureCopy(config);
}

bool RasterizerOpenGL::AccelerateFill(const Pica::MemoryFillConfig& config) {
    return res_cache.AccelerateFill(config);
}

bool RasterizerOpenGL::AccelerateDisplay(const Pica::FramebufferConfig& config,
                                         PAddr framebuffer_addr, u32 pixel_stride,
                                         ScreenInfo& screen_info) {
    if (framebuffer_addr == 0) {
        return false;
    }
    BORKED3DS_PROFILE("OpenGL", "Display");

    VideoCore::SurfaceParams src_params;
    src_params.addr = framebuffer_addr;
    src_params.width = std::min(config.width.Value(), pixel_stride);
    src_params.height = config.height;
    src_params.stride = pixel_stride;
    src_params.is_tiled = false;
    src_params.pixel_format = VideoCore::PixelFormatFromGPUPixelFormat(config.color_format);
    src_params.UpdateParams();

    const auto [src_surface_id, src_rect] =
        res_cache.GetSurfaceSubRect(src_params, VideoCore::ScaleMatch::Ignore, true);
    if (!src_surface_id) {
        return false;
    }

    const DebugScope scope{runtime,
                           Common::Vec4f{0.f, 1.f, 1.f, 1.f},
                           "RasterizerOpenGL::AccelerateDisplay ({}x{} {} at {:#X})",
                           src_params.width,
                           src_params.height,
                           VideoCore::PixelFormatAsString(src_params.pixel_format),
                           src_params.addr};

    const Surface& src_surface = res_cache.GetSurface(src_surface_id);
    const u32 scaled_width = src_surface.GetScaledWidth();
    const u32 scaled_height = src_surface.GetScaledHeight();

    screen_info.display_texcoords = Common::Rectangle<float>(
        (float)src_rect.bottom / (float)scaled_height, (float)src_rect.left / (float)scaled_width,
        (float)src_rect.top / (float)scaled_height, (float)src_rect.right / (float)scaled_width);

    screen_info.display_texture = src_surface.Handle();

    return true;
}

void RasterizerOpenGL::SyncClipEnabled() {
    state.clip_distance[1] = regs.rasterizer.clip_enable != 0;
}

void RasterizerOpenGL::SyncCullMode() {
    switch (regs.rasterizer.cull_mode) {
    case Pica::RasterizerRegs::CullMode::KeepAll:
        state.cull.enabled = false;
        break;
    case Pica::RasterizerRegs::CullMode::KeepClockWise:
        state.cull.enabled = true;
        state.cull.front_face = GL_CW;
        break;
    case Pica::RasterizerRegs::CullMode::KeepCounterClockWise:
        state.cull.enabled = true;
        state.cull.front_face = GL_CCW;
        break;
    default:
        LOG_CRITICAL(Render_OpenGL, "Unknown cull mode {}",
                     static_cast<u32>(regs.rasterizer.cull_mode.Value()));
        UNIMPLEMENTED();
        break;
    }
}

void RasterizerOpenGL::SyncBlendEnabled() {
    state.blend.enabled = (regs.framebuffer.output_merger.alphablend_enable == 1);
}

void RasterizerOpenGL::SyncBlendFuncs() {
    const bool has_minmax_factor = driver.HasBlendMinMaxFactor();

    state.blend.rgb_equation = PicaToGL::BlendEquation(
        regs.framebuffer.output_merger.alpha_blending.blend_equation_rgb, has_minmax_factor);
    state.blend.a_equation = PicaToGL::BlendEquation(
        regs.framebuffer.output_merger.alpha_blending.blend_equation_a, has_minmax_factor);
    state.blend.src_rgb_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_source_rgb);
    state.blend.dst_rgb_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_dest_rgb);
    state.blend.src_a_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_source_a);
    state.blend.dst_a_func =
        PicaToGL::BlendFunc(regs.framebuffer.output_merger.alpha_blending.factor_dest_a);

    if (has_minmax_factor) {
        return;
    }

    // Blending with min/max equations is emulated in the fragment shader so
    // configure blending to not modify the incoming fragment color.
    emulate_minmax_blend = false;
    if (state.EmulateColorBlend()) {
        emulate_minmax_blend = true;
        state.blend.rgb_equation = GL_FUNC_ADD;
        state.blend.src_rgb_func = GL_ONE;
        state.blend.dst_rgb_func = GL_ZERO;
    }
    if (state.EmulateAlphaBlend()) {
        emulate_minmax_blend = true;
        state.blend.a_equation = GL_FUNC_ADD;
        state.blend.src_a_func = GL_ONE;
        state.blend.dst_a_func = GL_ZERO;
    }
}

void RasterizerOpenGL::SyncBlendColor() {
    const auto blend_color = PicaToGL::ColorRGBA8(regs.framebuffer.output_merger.blend_const.raw);
    state.blend.color.red = blend_color[0];
    state.blend.color.green = blend_color[1];
    state.blend.color.blue = blend_color[2];
    state.blend.color.alpha = blend_color[3];

    if (blend_color != fs_uniform_block_data.data.blend_color) {
        fs_uniform_block_data.data.blend_color = blend_color;
        fs_uniform_block_data.dirty = true;
    }
}

void RasterizerOpenGL::SyncLogicOp() {
    state.logic_op = PicaToGL::LogicOp(regs.framebuffer.output_merger.logic_op);

    if (driver.IsOpenGLES()) {
        if (!regs.framebuffer.output_merger.alphablend_enable) {
            if (regs.framebuffer.output_merger.logic_op == Pica::FramebufferRegs::LogicOp::NoOp) {
                // Color output is disabled by logic operation. We use color write mask
                // to skip color but allow depth write.
                state.color_mask = {};
            }
        }
    }
}

void RasterizerOpenGL::SyncColorWriteMask() {
    if (driver.IsOpenGLES()) {
        if (!regs.framebuffer.output_merger.alphablend_enable) {
            if (regs.framebuffer.output_merger.logic_op == Pica::FramebufferRegs::LogicOp::NoOp) {
                // Color output is disabled by logic operation. We use color write mask
                // to skip color but allow depth write. Return early to avoid
                // overwriting this.
                return;
            }
        }
    }

    auto is_color_write_enabled = [&](u32 value) {
        return (regs.framebuffer.framebuffer.allow_color_write != 0 && value != 0) ? GL_TRUE
                                                                                   : GL_FALSE;
    };

    state.color_mask.red_enabled =
        is_color_write_enabled(regs.framebuffer.output_merger.red_enable);
    state.color_mask.green_enabled =
        is_color_write_enabled(regs.framebuffer.output_merger.green_enable);
    state.color_mask.blue_enabled =
        is_color_write_enabled(regs.framebuffer.output_merger.blue_enable);
    state.color_mask.alpha_enabled =
        is_color_write_enabled(regs.framebuffer.output_merger.alpha_enable);
}

void RasterizerOpenGL::SyncStencilWriteMask() {
    state.stencil.write_mask =
        (regs.framebuffer.framebuffer.allow_depth_stencil_write != 0)
            ? static_cast<GLuint>(regs.framebuffer.output_merger.stencil_test.write_mask)
            : 0;
}

void RasterizerOpenGL::SyncDepthWriteMask() {
    state.depth.write_mask = (regs.framebuffer.framebuffer.allow_depth_stencil_write != 0 &&
                              regs.framebuffer.output_merger.depth_write_enable)
                                 ? GL_TRUE
                                 : GL_FALSE;
}

void RasterizerOpenGL::SyncStencilTest() {
    state.stencil.test_enabled =
        regs.framebuffer.output_merger.stencil_test.enable &&
        regs.framebuffer.framebuffer.depth_format == Pica::FramebufferRegs::DepthFormat::D24S8;
    state.stencil.test_func =
        PicaToGL::CompareFunc(regs.framebuffer.output_merger.stencil_test.func);
    state.stencil.test_ref = regs.framebuffer.output_merger.stencil_test.reference_value;
    state.stencil.test_mask = regs.framebuffer.output_merger.stencil_test.input_mask;
    state.stencil.action_stencil_fail =
        PicaToGL::StencilOp(regs.framebuffer.output_merger.stencil_test.action_stencil_fail);
    state.stencil.action_depth_fail =
        PicaToGL::StencilOp(regs.framebuffer.output_merger.stencil_test.action_depth_fail);
    state.stencil.action_depth_pass =
        PicaToGL::StencilOp(regs.framebuffer.output_merger.stencil_test.action_depth_pass);
}

void RasterizerOpenGL::SyncDepthTest() {
    state.depth.test_enabled = regs.framebuffer.output_merger.depth_test_enable == 1 ||
                               regs.framebuffer.output_merger.depth_write_enable == 1;
    state.depth.test_func =
        regs.framebuffer.output_merger.depth_test_enable == 1
            ? PicaToGL::CompareFunc(regs.framebuffer.output_merger.depth_test_func)
            : GL_ALWAYS;
}

void RasterizerOpenGL::SyncAndUploadLUTsLF() {
    const bool is_gles = driver.IsOpenGLES();
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    constexpr std::size_t max_size =
        sizeof(Common::Vec2f) * 256 * Pica::LightingRegs::NumLightingSampler + // lighting
        sizeof(Common::Vec2f) * 128;                                           // fog

    if (!fs_uniform_block_data.lighting_lut_dirty_any && !fs_uniform_block_data.fog_lut_dirty) {
        return;
    }

    if (is_gles && !GLAD_GL_OES_texture_buffer) { //gvx64
        // Update 2D textures directly for the fallback path
        if (fs_uniform_block_data.lighting_lut_dirty_any) {
            for (unsigned index = 0; index < fs_uniform_block_data.lighting_lut_dirty.size();
                 index++) {
                if (fs_uniform_block_data.lighting_lut_dirty[index]) {
                    std::array<Common::Vec2f, 256> new_data;
                    const auto& source_lut = pica.lighting.luts[index];
                    std::transform(source_lut.begin(), source_lut.end(), new_data.begin(),
                                   [](const auto& entry) {
                                       return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                                   });

                    if (new_data != lighting_lut_data[index]) {
                        lighting_lut_data[index] = new_data;
                        glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
                        glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rg.handle);
                        // Each LUT has 256 entries, store them sequentially in the 2D
                        // texture
                        const int x_offset = (index % 4) * 256; // 4 LUTs per row
                        const int y_offset = index / 4;         // Move to next row every 4 LUTs
                        glTexSubImage2D(GL_TEXTURE_2D, 0, x_offset, y_offset, 256, 1, GL_RG,
                                        GL_FLOAT, new_data.data());
                        fs_uniform_block_data.data.lighting_lut_offset[index / 4][index % 4] =
                            x_offset + y_offset * 1024; // 1024 = 4 * 256 (width of texture)
                        fs_uniform_block_data.dirty = true;
                    }
                    fs_uniform_block_data.lighting_lut_dirty[index] = false;
                }
            }
            fs_uniform_block_data.lighting_lut_dirty_any = false;
        }

        if (fs_uniform_block_data.fog_lut_dirty) {
            std::array<Common::Vec2f, 128> new_data;
            std::transform(pica.fog.lut.begin(), pica.fog.lut.end(), new_data.begin(),
                           [](const auto& entry) {
                               return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                           });

            if (new_data != fog_lut_data) {
                fog_lut_data = new_data;
                glActiveTexture(TextureUnits::TextureBufferLUT_LF.Enum());
                glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_lf.handle);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 1, GL_RG, GL_FLOAT, new_data.data());
                fs_uniform_block_data.data.fog_lut_offset = 0;
                fs_uniform_block_data.dirty = true;
            }
            fs_uniform_block_data.fog_lut_dirty = false;
        }
    } else {
        // Original buffer update code
        std::size_t bytes_used = 0;
        glBindBuffer(GL_TEXTURE_BUFFER_OES, texture_lf_buffer.GetHandle());
        const auto [buffer, offset, invalidate] =
            texture_lf_buffer.Map(max_size, sizeof(Common::Vec4f));
//gvx64printf("../src/video_core/renderer_opengl/gl_rasterizer.cpp, lutslf(), offset = %08lx, invalidate = %08x\n",offset, invalidate); //gvx64
        // Sync the lighting luts
        if (fs_uniform_block_data.lighting_lut_dirty_any || invalidate) {
            for (unsigned index = 0; index < fs_uniform_block_data.lighting_lut_dirty.size(); index++) {
                if (fs_uniform_block_data.lighting_lut_dirty[index] || invalidate) {
                    std::array<Common::Vec2f, 256> new_data;
                    const auto& source_lut = pica.lighting.luts[index];
                    std::transform(source_lut.begin(), source_lut.end(), new_data.begin(),
                                   [](const auto& entry) {
                                       return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                                   });

                    if (new_data != lighting_lut_data[index] || invalidate) {
                        lighting_lut_data[index] = new_data;
                        std::memcpy(buffer + bytes_used, new_data.data(),
                                    new_data.size() * sizeof(Common::Vec2f));
                        fs_uniform_block_data.data.lighting_lut_offset[index / 4][index % 4] =
                            static_cast<GLint>((offset + bytes_used) / sizeof(Common::Vec2f));
                        fs_uniform_block_data.dirty = true;
                        bytes_used += new_data.size() * sizeof(Common::Vec2f);
                    }
                    fs_uniform_block_data.lighting_lut_dirty[index] = false;
                }
            }
            fs_uniform_block_data.lighting_lut_dirty_any = false;
        }

        // Sync the fog lut
        if (fs_uniform_block_data.fog_lut_dirty || invalidate) {
            std::array<Common::Vec2f, 128> new_data;

            std::transform(pica.fog.lut.begin(), pica.fog.lut.end(), new_data.begin(),
                           [](const auto& entry) {
                               return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                           });

            if (new_data != fog_lut_data || invalidate) {
                fog_lut_data = new_data;
                std::memcpy(buffer + bytes_used, new_data.data(),
                            new_data.size() * sizeof(Common::Vec2f));
                fs_uniform_block_data.data.fog_lut_offset =
                    static_cast<int>((offset + bytes_used) / sizeof(Common::Vec2f));
                fs_uniform_block_data.dirty = true;
                bytes_used += new_data.size() * sizeof(Common::Vec2f);
            }
            fs_uniform_block_data.fog_lut_dirty = false;
        }

        texture_lf_buffer.Unmap(bytes_used);
    }
}

void RasterizerOpenGL::SyncAndUploadLUTs() {
    const bool is_gles = driver.IsOpenGLES();
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    constexpr std::size_t max_size =
        sizeof(Common::Vec2f) * 128 * 3 + // proctex: noise + color + alpha
        sizeof(Common::Vec4f) * 256 +     // proctex
        sizeof(Common::Vec4f) * 256;      // proctex diff

    if (!fs_uniform_block_data.proctex_noise_lut_dirty &&
        !fs_uniform_block_data.proctex_color_map_dirty &&
        !fs_uniform_block_data.proctex_alpha_map_dirty &&
        !fs_uniform_block_data.proctex_lut_dirty && !fs_uniform_block_data.proctex_diff_lut_dirty) {
        return;
    }

    if (is_gles && !GLAD_GL_OES_texture_buffer) { //gvx64
        // Update 2D textures directly for the fallback path
        if (fs_uniform_block_data.proctex_noise_lut_dirty) {
            std::array<Common::Vec2f, 128> new_data;
            std::transform(pica.proctex.noise_table.begin(), pica.proctex.noise_table.end(),
                           new_data.begin(), [](const auto& entry) {
                               return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                           });

            if (new_data != proctex_noise_lut_data) {
                proctex_noise_lut_data = new_data;
                glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
                glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rg.handle);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 1, GL_RG, GL_FLOAT, new_data.data());
                fs_uniform_block_data.data.proctex_noise_lut_offset = 0;
                fs_uniform_block_data.dirty = true;
            }
            fs_uniform_block_data.proctex_noise_lut_dirty = false;
        }

        if (fs_uniform_block_data.proctex_color_map_dirty) {
            std::array<Common::Vec2f, 128> new_data;
            std::transform(pica.proctex.color_map_table.begin(), pica.proctex.color_map_table.end(),
                           new_data.begin(), [](const auto& entry) {
                               return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                           });

            if (new_data != proctex_color_map_data) {
                proctex_color_map_data = new_data;
                glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
                glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rg.handle);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 128, 0, 128, 1, GL_RG, GL_FLOAT, new_data.data());
                fs_uniform_block_data.data.proctex_color_map_offset = 128;
                fs_uniform_block_data.dirty = true;
            }
            fs_uniform_block_data.proctex_color_map_dirty = false;
        }

        if (fs_uniform_block_data.proctex_alpha_map_dirty) {
            std::array<Common::Vec2f, 128> new_data;
            std::transform(pica.proctex.alpha_map_table.begin(), pica.proctex.alpha_map_table.end(),
                           new_data.begin(), [](const auto& entry) {
                               return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                           });

            if (new_data != proctex_alpha_map_data) {
                proctex_alpha_map_data = new_data;
                glActiveTexture(TextureUnits::TextureBufferLUT_RG.Enum());
                glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rg.handle);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 256, 0, 128, 1, GL_RG, GL_FLOAT, new_data.data());
                fs_uniform_block_data.data.proctex_alpha_map_offset = 256;
                fs_uniform_block_data.dirty = true;
            }
            fs_uniform_block_data.proctex_alpha_map_dirty = false;
        }

        if (fs_uniform_block_data.proctex_lut_dirty) {
            std::array<Common::Vec4f, 256> new_data;

            std::transform(pica.proctex.color_table.begin(), pica.proctex.color_table.end(),
                           new_data.begin(), [](const auto& entry) {
                               auto rgba = entry.ToVector() / 255.0f;
                               return Common::Vec4f{rgba.r(), rgba.g(), rgba.b(), rgba.a()};
                           });

            if (new_data != proctex_lut_data) {
                proctex_lut_data = new_data;
                glActiveTexture(TextureUnits::TextureBufferLUT_RGBA.Enum());
                glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rgba.handle);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_FLOAT, new_data.data());
                fs_uniform_block_data.data.proctex_lut_offset = 0;
                fs_uniform_block_data.dirty = true;
            }
            fs_uniform_block_data.proctex_lut_dirty = false;
        }

        if (fs_uniform_block_data.proctex_diff_lut_dirty) {
            std::array<Common::Vec4f, 256> new_data;

            std::transform(pica.proctex.color_diff_table.begin(),
                           pica.proctex.color_diff_table.end(), new_data.begin(),
                           [](const auto& entry) {
                               auto rgba = entry.ToVector() / 255.0f;
                               return Common::Vec4f{rgba.r(), rgba.g(), rgba.b(), rgba.a()};
                           });

            if (new_data != proctex_diff_lut_data) {
                proctex_diff_lut_data = new_data;
                glActiveTexture(TextureUnits::TextureBufferLUT_RGBA.Enum());
                glBindTexture(GL_TEXTURE_2D, texture_buffer_lut_rgba.handle);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, 256, 1, GL_RGBA, GL_FLOAT, new_data.data());
                fs_uniform_block_data.data.proctex_diff_lut_offset = 256;
                fs_uniform_block_data.dirty = true;
            }
            fs_uniform_block_data.proctex_diff_lut_dirty = false;
        }
    } else {
        // Original buffer update code
        std::size_t bytes_used = 0;
        glBindBuffer(GL_TEXTURE_BUFFER_OES, texture_buffer.GetHandle());
        const auto [buffer, offset, invalidate] = texture_buffer.Map(max_size, sizeof(Common::Vec4f));

        // helper function for SyncProcTexNoiseLUT/ColorMap/AlphaMap
        const auto sync_proc_tex_value_lut =
            [this, buffer = buffer, offset = offset, invalidate = invalidate, &bytes_used](
                const auto& lut, std::array<Common::Vec2f, 128>& lut_data, GLint& lut_offset) {
                std::array<Common::Vec2f, 128> new_data;
                std::transform(lut.begin(), lut.end(), new_data.begin(), [](const auto& entry) {
                    return Common::Vec2f{entry.ToFloat(), entry.DiffToFloat()};
                });

                if (new_data != lut_data || invalidate) {
                    lut_data = new_data;
                    std::memcpy(buffer + bytes_used, new_data.data(),
                                new_data.size() * sizeof(Common::Vec2f));
                    lut_offset = static_cast<GLint>((offset + bytes_used) / sizeof(Common::Vec2f));
                    fs_uniform_block_data.dirty = true;
                    bytes_used += new_data.size() * sizeof(Common::Vec2f);
                }
            };

        // Sync the proctex noise lut
        if (fs_uniform_block_data.proctex_noise_lut_dirty || invalidate) {
            sync_proc_tex_value_lut(pica.proctex.noise_table, proctex_noise_lut_data,
                                    fs_uniform_block_data.data.proctex_noise_lut_offset);
            fs_uniform_block_data.proctex_noise_lut_dirty = false;
        }

        // Sync the proctex color map
        if (fs_uniform_block_data.proctex_color_map_dirty || invalidate) {
            sync_proc_tex_value_lut(pica.proctex.color_map_table, proctex_color_map_data,
                                    fs_uniform_block_data.data.proctex_color_map_offset);
            fs_uniform_block_data.proctex_color_map_dirty = false;
        }

        // Sync the proctex alpha map
        if (fs_uniform_block_data.proctex_alpha_map_dirty || invalidate) {
            sync_proc_tex_value_lut(pica.proctex.alpha_map_table, proctex_alpha_map_data,
                                   fs_uniform_block_data.data.proctex_alpha_map_offset);
            fs_uniform_block_data.proctex_alpha_map_dirty = false;
        }

        // Sync the proctex lut
        if (fs_uniform_block_data.proctex_lut_dirty || invalidate) {
            std::array<Common::Vec4f, 256> new_data;

            std::transform(pica.proctex.color_table.begin(), pica.proctex.color_table.end(),
                           new_data.begin(), [](const auto& entry) {
                               auto rgba = entry.ToVector() / 255.0f;
                               return Common::Vec4f{rgba.r(), rgba.g(), rgba.b(), rgba.a()};
                           });

            if (new_data != proctex_lut_data || invalidate) {
                proctex_lut_data = new_data;
                std::memcpy(buffer + bytes_used, new_data.data(),
                            new_data.size() * sizeof(Common::Vec4f));
                fs_uniform_block_data.data.proctex_lut_offset =
                    static_cast<GLint>((offset + bytes_used) / sizeof(Common::Vec4f));
                fs_uniform_block_data.dirty = true;
                bytes_used += new_data.size() * sizeof(Common::Vec4f);
            }
            fs_uniform_block_data.proctex_lut_dirty = false;
        }

        // Sync the proctex difference lut
        if (fs_uniform_block_data.proctex_diff_lut_dirty || invalidate) {
            std::array<Common::Vec4f, 256> new_data;

            std::transform(pica.proctex.color_diff_table.begin(), pica.proctex.color_diff_table.end(),
                           new_data.begin(), [](const auto& entry) {
                               auto rgba = entry.ToVector() / 255.0f;
                               return Common::Vec4f{rgba.r(), rgba.g(), rgba.b(), rgba.a()};
                           });

            if (new_data != proctex_diff_lut_data || invalidate) {
                proctex_diff_lut_data = new_data;
                std::memcpy(buffer + bytes_used, new_data.data(),
                            new_data.size() * sizeof(Common::Vec4f));
                fs_uniform_block_data.data.proctex_diff_lut_offset =
                    static_cast<GLint>((offset + bytes_used) / sizeof(Common::Vec4f));
                fs_uniform_block_data.dirty = true;
                bytes_used += new_data.size() * sizeof(Common::Vec4f);
            }
            fs_uniform_block_data.proctex_diff_lut_dirty = false;
        }

        texture_buffer.Unmap(bytes_used);
    }
}

void RasterizerOpenGL::UploadUniforms(bool accelerate_draw) {
    // glBindBufferRange also changes the generic buffer binding point, so we sync
    // the state first.
    state.draw.uniform_buffer = uniform_buffer.GetHandle();
    state.Apply();

    const bool sync_vs_pica = accelerate_draw;
    const bool sync_vs = vs_uniform_block_data.dirty;
    const bool sync_fs = fs_uniform_block_data.dirty;
    if (!sync_vs_pica && !sync_vs && !sync_fs) {
        return;
    }

    std::size_t uniform_size =
        uniform_size_aligned_vs_pica + uniform_size_aligned_vs + uniform_size_aligned_fs;
    std::size_t used_bytes = 0;

    const auto [uniforms, offset, invalidate] =
        uniform_buffer.Map(uniform_size, uniform_buffer_alignment);

    if (sync_vs || invalidate) {
        std::memcpy(uniforms + used_bytes, &vs_uniform_block_data.data,
                    sizeof(vs_uniform_block_data.data));
        glBindBufferRange(GL_UNIFORM_BUFFER, UniformBindings::VSData, uniform_buffer.GetHandle(),
                          offset + used_bytes, sizeof(vs_uniform_block_data.data));
        vs_uniform_block_data.dirty = false;
        used_bytes += uniform_size_aligned_vs;
    }

    if (sync_fs || invalidate) {
        std::memcpy(uniforms + used_bytes, &fs_uniform_block_data.data,
                    sizeof(fs_uniform_block_data.data));
        glBindBufferRange(GL_UNIFORM_BUFFER, UniformBindings::FSData, uniform_buffer.GetHandle(),
                          offset + used_bytes, sizeof(fs_uniform_block_data.data));
        fs_uniform_block_data.dirty = false;
        used_bytes += uniform_size_aligned_fs;
    }

    if (sync_vs_pica) {
        VSPicaUniformData vs_uniforms;
        vs_uniforms.uniforms.SetFromRegs(regs.vs, pica.vs_setup);
        std::memcpy(uniforms + used_bytes, &vs_uniforms, sizeof(vs_uniforms));
        glBindBufferRange(GL_UNIFORM_BUFFER, UniformBindings::VSPicaData,
                          uniform_buffer.GetHandle(), offset + used_bytes, sizeof(vs_uniforms));
        used_bytes += uniform_size_aligned_vs_pica;
    }

    uniform_buffer.Unmap(used_bytes);
}

} // namespace OpenGL
