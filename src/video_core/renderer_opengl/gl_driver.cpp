// Copyright 2022 Citra Emulator Project
// Copyright 2024 Borked3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/gl.h>
#include "common/assert.h"
#include "common/settings.h"
#include "video_core/custom_textures/custom_format.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_vars.h"

namespace OpenGL {

DECLARE_ENUM_FLAG_OPERATORS(DriverBug);

inline std::string_view GetSource(GLenum source) {
#define RET(s)                                                                                     \
    case GL_DEBUG_SOURCE_##s:                                                                      \
        return #s
    switch (source) {
        RET(API);
        RET(WINDOW_SYSTEM);
        RET(SHADER_COMPILER);
        RET(THIRD_PARTY);
        RET(APPLICATION);
        RET(OTHER);
    default:
        UNREACHABLE();
    }
#undef RET

    return std::string_view{};
}

inline std::string_view GetType(GLenum type) {
#define RET(t)                                                                                     \
    case GL_DEBUG_TYPE_##t:                                                                        \
        return #t
    switch (type) {
        RET(ERROR);
        RET(DEPRECATED_BEHAVIOR);
        RET(UNDEFINED_BEHAVIOR);
        RET(PORTABILITY);
        RET(PERFORMANCE);
        RET(OTHER);
        RET(MARKER);
        RET(POP_GROUP);
        RET(PUSH_GROUP);
    default:
        UNREACHABLE();
    }
#undef RET

    return std::string_view{};
}

static void GLAD_API_PTR DebugHandler(GLenum source, GLenum type, GLuint id, GLenum severity,
                                      GLsizei length, const GLchar* message,
                                      const void* user_param) {
    auto level = Common::Log::Level::Info;
    GLint majorVersion = 0, minorVersion = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

    if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
        switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH_KHR:
            level = Common::Log::Level::Critical;
            break;
        case GL_DEBUG_SEVERITY_MEDIUM_KHR:
            level = Common::Log::Level::Warning;
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION_KHR:
        case GL_DEBUG_SEVERITY_LOW_KHR:
            level = Common::Log::Level::Debug;
            break;
        }
    } else {
        switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            level = Common::Log::Level::Critical;
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            level = Common::Log::Level::Warning;
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION:
        case GL_DEBUG_SEVERITY_LOW:
            level = Common::Log::Level::Debug;
            break;
        }
    }
    LOG_GENERIC(Common::Log::Class::Render_OpenGL, level, "{} {} {}: {}", GetSource(source),
                GetType(type), id, message);
}

Driver::Driver() {
    const bool enable_debug = Settings::values.renderer_debug.GetValue();
    if (enable_debug) {

        GLint majorVersion = 0, minorVersion = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &minorVersion);

        if (OpenGL::GLES && majorVersion == 3 && minorVersion < 2) {
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_KHR);
            glDebugMessageCallbackKHR(DebugHandler, nullptr);
        } else {
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(DebugHandler, nullptr);
        }
    }

    ReportDriverInfo();
    DeduceGLES();
    DeduceVendor();
    CheckExtensionSupport();
    FindBugs();
}

Driver::~Driver() = default;

bool Driver::HasBug(DriverBug bug) const {
    return True(bugs & bug);
}

bool Driver::HasExtension(std::string_view name) const {
    if (is_gles) {
        // For OpenGL ES, we need to check extensions one by one
        GLint num_extensions;
        glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
        for (GLint i = 0; i < num_extensions; ++i) {
            const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            if (name == extension) {
                return true;
            }
        }
        return false;
    } else {
        // For desktop OpenGL, we can use GLAD's extension checking
        // Convert extension name to string for comparison
        std::string ext_name(name);
        if (ext_name.starts_with("GL_")) {
            ext_name = ext_name.substr(3); // Remove "GL_" prefix
        }

        // AMD Extensions
        if (ext_name == "AMD_blend_minmax_factor")
            return GLAD_GL_AMD_blend_minmax_factor;

        // ARB Extensions
        if (ext_name == "ARB_buffer_storage")
            return GLAD_GL_ARB_buffer_storage;
        if (ext_name == "ARB_clear_texture")
            return GLAD_GL_ARB_clear_texture;
        if (ext_name == "ARB_fragment_shader_interlock")
            return GLAD_GL_ARB_fragment_shader_interlock;
        if (ext_name == "ARB_get_texture_sub_image")
            return GLAD_GL_ARB_get_texture_sub_image;
        if (ext_name == "ARB_shader_image_load_store")
            return GLAD_GL_ARB_shader_image_load_store;
        if (ext_name == "ARB_texture_compression_bptc")
            return GLAD_GL_ARB_texture_compression_bptc;
        if (ext_name == "ARB_separate_shader_objects")
            return GLAD_GL_ARB_separate_shader_objects;

        // ARM Extensions
        if (ext_name == "ARM_shader_framebuffer_fetch")
            return GLAD_GL_ARM_shader_framebuffer_fetch;

        // EXT Extensions
        if (ext_name == "EXT_buffer_storage")
            return GLAD_GL_EXT_buffer_storage;
        if (ext_name == "EXT_clear_texture")
            return GLAD_GL_EXT_clear_texture;
        if (ext_name == "EXT_clip_cull_distance")
            return GLAD_GL_EXT_clip_cull_distance;
        if (ext_name == "EXT_shader_framebuffer_fetch")
            return GLAD_GL_EXT_shader_framebuffer_fetch;
        if (ext_name == "EXT_texture_buffer")
            return GLAD_GL_EXT_texture_buffer;
        if (ext_name == "EXT_texture_compression_bptc")
            return GLAD_GL_EXT_texture_compression_bptc;
        if (ext_name == "EXT_texture_compression_s3tc")
            return GLAD_GL_EXT_texture_compression_s3tc;
        if (ext_name == "EXT_color_buffer_half_float")
            return GLAD_GL_EXT_color_buffer_half_float;
        if (ext_name == "EXT_debug_label")
            return GLAD_GL_EXT_debug_label;
        if (ext_name == "EXT_debug_marker")
            return GLAD_GL_EXT_debug_marker;
        if (ext_name == "EXT_separate_shader_objects")
            return GLAD_GL_EXT_separate_shader_objects;
        if (ext_name == "EXT_shadow_samplers")
            return GLAD_GL_EXT_shadow_samplers;
        if (ext_name == "EXT_texture_sRGB_decode")
            return GLAD_GL_EXT_texture_sRGB_decode;
        if (ext_name == "EXT_texture_type_2_10_10_10_REV")
            return GLAD_GL_EXT_texture_type_2_10_10_10_REV;
        if (ext_name == "EXT_texture_filter_anisotropic")
            return GLAD_GL_EXT_texture_filter_anisotropic;
        if (ext_name == "EXT_texture_format_BGRA8888")
            return GLAD_GL_EXT_texture_format_BGRA8888;
        if (ext_name == "EXT_texture_storage")
            return GLAD_GL_EXT_texture_storage;
        if (ext_name == "EXT_unpack_subimage")
            return GLAD_GL_EXT_unpack_subimage;
        if (ext_name == "EXT_geometry_shader")
            return GLAD_GL_EXT_geometry_shader;

        // INTEL Extensions
        if (ext_name == "INTEL_fragment_shader_ordering")
            return GLAD_GL_INTEL_fragment_shader_ordering;

        // KHR Extensions
        if (ext_name == "KHR_texture_compression_astc_ldr")
            return GLAD_GL_KHR_texture_compression_astc_ldr;
        if (ext_name == "KHR_debug")
            return GLAD_GL_KHR_debug;

        // NV Extensions
        if (ext_name == "NV_blend_minmax_factor")
            return GLAD_GL_NV_blend_minmax_factor;
        if (ext_name == "NV_fragment_shader_interlock")
            return GLAD_GL_NV_fragment_shader_interlock;

        // OES Extensions
        if (ext_name == "OES_depth_texture")
            return GLAD_GL_OES_depth_texture;
        if (ext_name == "OES_packed_depth_stencil")
            return GLAD_GL_OES_packed_depth_stencil;
        if (ext_name == "OES_standard_derivatives")
            return GLAD_GL_OES_standard_derivatives;
        if (ext_name == "OES_texture_float")
            return GLAD_GL_OES_texture_float;
        if (ext_name == "OES_texture_half_float")
            return GLAD_GL_OES_texture_half_float;
        if (ext_name == "OES_texture_npot")
            return GLAD_GL_OES_texture_npot;
        if (ext_name == "OES_vertex_array_object")
            return GLAD_GL_OES_vertex_array_object;
        if (ext_name == "OES_texture_view")
            return GLAD_GL_OES_texture_view;
        if (ext_name == "OES_texture_buffer")
            return GLAD_GL_OES_texture_buffer;

        // For any other extensions, check using glGetStringi
        GLint num_extensions;
        glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
        for (GLint i = 0; i < num_extensions; ++i) {
            const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
            if (name == extension) {
                return true;
            }
        }
        return false;
    }
}

bool Driver::HasDebugTool() {
    GLint num_extensions;
    glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
    for (GLuint index = 0; index < static_cast<GLuint>(num_extensions); ++index) {
        const auto name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, index));
        if (!std::strcmp(name, "GL_EXT_debug_tool")) {
            return true;
        }
    }
    return false;
}

bool Driver::IsCustomFormatSupported(VideoCore::CustomPixelFormat format) const {
    switch (format) {
    case VideoCore::CustomPixelFormat::RGBA8:
        return true;
    case VideoCore::CustomPixelFormat::BC1:
    case VideoCore::CustomPixelFormat::BC3:
    case VideoCore::CustomPixelFormat::BC5:
        return ext_texture_compression_s3tc;
    case VideoCore::CustomPixelFormat::BC7:
#ifdef __ANDROID__
        return ext_texture_compression_bptc;
#else
        return arb_texture_compression_bptc;
#endif
    case VideoCore::CustomPixelFormat::ASTC4:
    case VideoCore::CustomPixelFormat::ASTC6:
    case VideoCore::CustomPixelFormat::ASTC8:
        return is_gles;
    default:
        return false;
    }
}

void Driver::ReportDriverInfo() {
    // Report the context version and the vendor string
    gl_version = std::string_view{reinterpret_cast<const char*>(glGetString(GL_VERSION))};
    gpu_vendor = std::string_view{reinterpret_cast<const char*>(glGetString(GL_VENDOR))};
    gpu_model = std::string_view{reinterpret_cast<const char*>(glGetString(GL_RENDERER))};

    LOG_INFO(Render_OpenGL, "GL_VERSION: {}", gl_version);
    LOG_INFO(Render_OpenGL, "GL_VENDOR: {}", gpu_vendor);
    LOG_INFO(Render_OpenGL, "GL_RENDERER: {}", gpu_model);
}

void Driver::DeduceGLES() {
    // According to the spec, all GLES version strings must start with "OpenGL ES".
    is_gles = gl_version.starts_with("OpenGL ES");

    // TODO: Eliminate this global state and replace with driver references.
    OpenGL::GLES = is_gles;
}

void Driver::DeduceVendor() {
    if (gpu_vendor.find("NVIDIA") != gpu_vendor.npos) {
        vendor = Vendor::Nvidia;
    } else if ((gpu_vendor.find("ATI") != gpu_vendor.npos) ||
               (gpu_vendor.find("AMD") != gpu_vendor.npos) ||
               (gpu_vendor.find("Advanced Micro Devices") != gpu_vendor.npos)) {
        vendor = Vendor::AMD;
    } else if (gpu_vendor.find("Intel") != gpu_vendor.npos) {
        vendor = Vendor::Intel;
    } else if (gpu_vendor.find("ARM") != gpu_vendor.npos) {
        vendor = Vendor::ARM;
    } else if (gpu_vendor.find("Qualcomm") != gpu_vendor.npos) {
        vendor = Vendor::Qualcomm;
    } else if (gpu_vendor.find("Samsung") != gpu_vendor.npos) {
        vendor = Vendor::Samsung;
    } else if (gpu_vendor.find("GDI Generic") != gpu_vendor.npos) {
        vendor = Vendor::Generic;
    }
}

void Driver::CheckExtensionSupport() {
    ext_buffer_storage = GLAD_GL_EXT_buffer_storage;
    arb_buffer_storage = GLAD_GL_ARB_buffer_storage;
    arb_clear_texture = GLAD_GL_ARB_clear_texture;
    ext_clear_texture = GLAD_GL_EXT_clear_texture;
    arb_get_texture_sub_image = GLAD_GL_ARB_get_texture_sub_image;
    arb_shader_image_load_store = GLAD_GL_ARB_shader_image_load_store;
    arb_texture_compression_bptc = GLAD_GL_ARB_texture_compression_bptc;
    ext_texture_compression_bptc = GLAD_GL_EXT_texture_compression_bptc;
    ext_texture_buffer = !is_gles || GLAD_GL_EXT_texture_buffer;
    oes_texture_buffer = !is_gles || GLAD_GL_OES_texture_buffer; //gvx64
    clip_cull_distance = !is_gles || GLAD_GL_EXT_clip_cull_distance;
    ext_texture_compression_s3tc = GLAD_GL_EXT_texture_compression_s3tc;
    ext_shader_framebuffer_fetch = GLAD_GL_EXT_shader_framebuffer_fetch;
    arm_shader_framebuffer_fetch = GLAD_GL_ARM_shader_framebuffer_fetch;
    arb_fragment_shader_interlock = GLAD_GL_ARB_fragment_shader_interlock;
    nv_fragment_shader_interlock = GLAD_GL_NV_fragment_shader_interlock;
    intel_fragment_shader_ordering = GLAD_GL_INTEL_fragment_shader_ordering;
    blend_minmax_factor = GLAD_GL_AMD_blend_minmax_factor || GLAD_GL_NV_blend_minmax_factor;
    is_suitable = GLAD_GL_VERSION_4_3 || GLAD_GL_ES_VERSION_3_1;
}

void Driver::FindBugs() {
#ifdef __unix__
    const bool is_linux = true;
#else
    const bool is_linux = false;
#endif

    // TODO: Check if these have been fixed in the newer driver
    if (vendor == Vendor::AMD) {
        bugs |= DriverBug::ShaderStageChangeFreeze | DriverBug::VertexArrayOutOfBound;
    }

    if (vendor == Vendor::AMD || (vendor == Vendor::Intel && !is_linux)) {
        bugs |= DriverBug::BrokenTextureView;
    }

    if (vendor == Vendor::Intel && !is_linux) {
        bugs |= DriverBug::BrokenClearTexture;
    }

    if (vendor == Vendor::ARM && gpu_model.find("Mali") != gpu_model.npos) {
        constexpr GLint MIN_TEXTURE_BUFFER_SIZE = static_cast<GLint>((1 << 16));
        GLint max_texel_buffer_size;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texel_buffer_size);
        if (max_texel_buffer_size == MIN_TEXTURE_BUFFER_SIZE) {
            bugs |= DriverBug::SlowTextureBufferWithBigSize;
        }
    }
}

} // namespace OpenGL
