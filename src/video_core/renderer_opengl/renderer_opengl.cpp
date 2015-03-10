// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/settings.h"
#include "core/hw/gpu.h"
#include "core/mem_map.h"

#include "common/emu_window.h"
#include "common/profiler_reporting.h"

#include "video_core/video_core.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/gl_shaders.h"

#include "video_core/pica.h"
#include "video_core/vertex_shader.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/utils.h"
#include "video_core/color.h"

#include <algorithm>

std::map<u32, GLuint> g_tex_cache;

std::vector<struct GameWorldVertex> g_vertex_batch;

u32 g_last_fb_color_addr = -1;
u32 g_last_fb_color_format;
u32 g_last_fb_depth_addr = -1;
u32 g_last_fb_depth_format;

/**
 * Vertex structure that the drawn screen rectangles are composed of.
 */
struct ScreenRectVertex {
    ScreenRectVertex(GLfloat x, GLfloat y, GLfloat u, GLfloat v) {
        position[0] = x;
        position[1] = y;
        tex_coord[0] = u;
        tex_coord[1] = v;
    }

    GLfloat position[2];
    GLfloat tex_coord[2];
};

/**
* Vertex structure that the hardware renderer uses.
*/
struct GameWorldVertex {
    GameWorldVertex(const Pica::VertexShader::OutputVertex& v) {
        position[0] = v.pos.x.ToFloat32();
        position[1] = v.pos.y.ToFloat32();
        position[2] = v.pos.z.ToFloat32();
        position[3] = v.pos.w.ToFloat32();
        color[0] = v.color.x.ToFloat32();
        color[1] = v.color.y.ToFloat32();
        color[2] = v.color.z.ToFloat32();
        color[3] = v.color.w.ToFloat32();
        tex_coord0[0] = v.tc0.x.ToFloat32();
        tex_coord0[1] = v.tc0.y.ToFloat32();
        tex_coord1[0] = v.tc1.x.ToFloat32();
        tex_coord1[1] = v.tc1.y.ToFloat32();
        tex_coord2[0] = v.tc2.x.ToFloat32();
        tex_coord2[1] = v.tc2.y.ToFloat32();
    }

    GLfloat position[4];
    GLfloat color[4];
    GLfloat tex_coord0[2];
    GLfloat tex_coord1[2];
    GLfloat tex_coord2[2];
};

/**
 * Defines a 1:1 pixel ortographic projection matrix with (0,0) on the top-left
 * corner and (width, height) on the lower-bottom.
 *
 * The projection part of the matrix is trivial, hence these operations are represented
 * by a 3x2 matrix.
 */
static std::array<GLfloat, 3*2> MakeOrthographicMatrix(const float width, const float height) {
    std::array<GLfloat, 3*2> matrix;

    matrix[0] = 2.f / width; matrix[2] = 0.f;           matrix[4] = -1.f;
    matrix[1] = 0.f;         matrix[3] = -2.f / height; matrix[5] = 1.f;
    // Last matrix row is implicitly assumed to be [0, 0, 1].

    return matrix;
}

/// RendererOpenGL constructor
RendererOpenGL::RendererOpenGL() {
    resolution_width  = std::max(VideoCore::kScreenTopWidth, VideoCore::kScreenBottomWidth);
    resolution_height = VideoCore::kScreenTopHeight + VideoCore::kScreenBottomHeight;
}

/// RendererOpenGL destructor
RendererOpenGL::~RendererOpenGL() {
}

/// Swap buffers (render frame)
void RendererOpenGL::SwapBuffers() {
    render_window->MakeCurrent();

    for(int i : {0, 1}) {
        const auto& framebuffer = GPU::g_regs.framebuffer_config[i];

        if (textures[i].width != (GLsizei)framebuffer.width ||
            textures[i].height != (GLsizei)framebuffer.height ||
            textures[i].format != framebuffer.color_format) {
            // Reallocate texture if the framebuffer size has changed.
            // This is expected to not happen very often and hence should not be a
            // performance problem.
            ConfigureFramebufferTexture(textures[i], framebuffer);
        }

        LoadFBToActiveGLTexture(GPU::g_regs.framebuffer_config[i], textures[i]);
    }

    glBindVertexArray(vertex_array_handle);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    DrawScreens();

    glBindVertexArray(hw_vertex_array_handle);
    glUseProgram(hw_program_id);

    auto& profiler = Common::Profiling::GetProfilingManager();
    profiler.FinishFrame();
    {
        auto aggregator = Common::Profiling::GetTimingResultsAggregator();
        aggregator->AddFrame(profiler.GetPreviousFrameResults());
    }

    // Swap buffers
    render_window->PollEvents();
    render_window->SwapBuffers();

    profiler.BeginFrame();

    if (Settings::values.gfx_backend.substr(0, Settings::values.gfx_backend.find_first_of(" #")).compare("OGL") == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, hw_framebuffer);
        glViewport(0, 0, hw_fb_texture.width, hw_fb_texture.height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

/**
 * Loads framebuffer from emulated memory into the active OpenGL texture.
 */
void RendererOpenGL::LoadFBToActiveGLTexture(const GPU::Regs::FramebufferConfig& framebuffer,
                                             const TextureInfo& texture) {

    const VAddr framebuffer_vaddr = Memory::PhysicalToVirtualAddress(
        framebuffer.active_fb == 0 ? framebuffer.address_left1 : framebuffer.address_left2);

    LOG_TRACE(Render_OpenGL, "0x%08x bytes from 0x%08x(%dx%d), fmt %x",
        framebuffer.stride * framebuffer.height,
        framebuffer_vaddr, (int)framebuffer.width,
        (int)framebuffer.height, (int)framebuffer.format);

    const u8* framebuffer_data = Memory::GetPointer(framebuffer_vaddr);

    int bpp = GPU::Regs::BytesPerPixel(framebuffer.color_format);
    size_t pixel_stride = framebuffer.stride / bpp;

    // OpenGL only supports specifying a stride in units of pixels, not bytes, unfortunately
    ASSERT(pixel_stride * bpp == framebuffer.stride);

    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT, which by default
    // only allows rows to have a memory alignement of 4.
    ASSERT(pixel_stride % 4 == 0);

    glBindTexture(GL_TEXTURE_2D, texture.handle);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)pixel_stride);

    // Update existing texture
    // TODO: Test what happens on hardware when you change the framebuffer dimensions so that they
    //       differ from the LCD resolution.
    // TODO: Applications could theoretically crash Citra here by specifying too large
    //       framebuffer sizes. We should make sure that this cannot happen.
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, framebuffer.width, framebuffer.height,
        texture.gl_format, texture.gl_type, framebuffer_data);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    glBindTexture(GL_TEXTURE_2D, 0);
}

/**
 * Initializes the OpenGL state and creates persistent objects.
 */
void RendererOpenGL::InitOpenGLObjects() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Link shaders and get variable locations
    program_id = ShaderUtil::LoadShaders(GLShaders::g_vertex_shader, GLShaders::g_fragment_shader);
    uniform_modelview_matrix = glGetUniformLocation(program_id, "modelview_matrix");
    uniform_color_texture = glGetUniformLocation(program_id, "color_texture");
    attrib_position = glGetAttribLocation(program_id, "vert_position");
    attrib_tex_coord = glGetAttribLocation(program_id, "vert_tex_coord");

    // Generate VBO handle for drawing
    glGenBuffers(1, &vertex_buffer_handle);

    // Generate VAO
    glGenVertexArrays(1, &vertex_array_handle);
    glBindVertexArray(vertex_array_handle);

    // Attach vertex data to VAO
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_handle);
    glBufferData(GL_ARRAY_BUFFER, sizeof(ScreenRectVertex) * 4, nullptr, GL_STREAM_DRAW);
    glVertexAttribPointer(attrib_position,  2, GL_FLOAT, GL_FALSE, sizeof(ScreenRectVertex), (GLvoid*)offsetof(ScreenRectVertex, position));
    glVertexAttribPointer(attrib_tex_coord, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenRectVertex), (GLvoid*)offsetof(ScreenRectVertex, tex_coord));
    glEnableVertexAttribArray(attrib_position);
    glEnableVertexAttribArray(attrib_tex_coord);

    // Allocate textures for each screen
    for (auto& texture : textures) {
        glGenTextures(1, &texture.handle);

        // Allocation of storage is deferred until the first frame, when we
        // know the framebuffer size.

        glBindTexture(GL_TEXTURE_2D, texture.handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Hardware renderer setup
    hw_program_id = ShaderUtil::LoadShaders(GLShaders::g_vertex_shader_hw, GLShaders::g_fragment_shader_hw);
    hw_attrib_position = glGetAttribLocation(hw_program_id, "vert_position");
    hw_attrib_color = glGetAttribLocation(hw_program_id, "vert_color");
    hw_attrib_texcoords[0] = glGetAttribLocation(hw_program_id, "vert_texcoord0");
    hw_attrib_texcoords[1] = glGetAttribLocation(hw_program_id, "vert_texcoord1");
    hw_attrib_texcoords[2] = glGetAttribLocation(hw_program_id, "vert_texcoord2");

    hw_uniform_alphatest_func = glGetUniformLocation(hw_program_id, "alphatest_func");
    hw_uniform_alphatest_ref = glGetUniformLocation(hw_program_id, "alphatest_ref");

    hw_uniform_tex = glGetUniformLocation(hw_program_id, "tex");
    hw_uniform_tevs = glGetUniformLocation(hw_program_id, "tevs");

    glUniform1i(hw_uniform_tex, 0);
    glUniform1i(hw_uniform_tex + 1, 1);
    glUniform1i(hw_uniform_tex + 2, 2);

    glGenBuffers(1, &hw_vertex_buffer_handle);

    // Generate VAO
    glGenVertexArrays(1, &hw_vertex_array_handle);
    glBindVertexArray(hw_vertex_array_handle);

    // Attach vertex data to VAO
    glBindBuffer(GL_ARRAY_BUFFER, hw_vertex_buffer_handle);

    glUseProgram(hw_program_id);

    glVertexAttribPointer(hw_attrib_position, 4, GL_FLOAT, GL_FALSE, sizeof(GameWorldVertex), (GLvoid*)offsetof(GameWorldVertex, position));
    glVertexAttribPointer(hw_attrib_color, 4, GL_FLOAT, GL_FALSE, sizeof(GameWorldVertex), (GLvoid*)offsetof(GameWorldVertex, color));
    glVertexAttribPointer(hw_attrib_texcoords[0], 2, GL_FLOAT, GL_FALSE, sizeof(GameWorldVertex), (GLvoid*)offsetof(GameWorldVertex, tex_coord0));
    glVertexAttribPointer(hw_attrib_texcoords[1], 2, GL_FLOAT, GL_FALSE, sizeof(GameWorldVertex), (GLvoid*)offsetof(GameWorldVertex, tex_coord1));
    glVertexAttribPointer(hw_attrib_texcoords[2], 2, GL_FLOAT, GL_FALSE, sizeof(GameWorldVertex), (GLvoid*)offsetof(GameWorldVertex, tex_coord2));
    glEnableVertexAttribArray(hw_attrib_position);
    glEnableVertexAttribArray(hw_attrib_color);
    glEnableVertexAttribArray(hw_attrib_texcoords[0]);
    glEnableVertexAttribArray(hw_attrib_texcoords[1]);
    glEnableVertexAttribArray(hw_attrib_texcoords[2]);

    glGenTextures(1, &hw_fb_texture.handle);

    glBindTexture(GL_TEXTURE_2D, hw_fb_texture.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &hw_framebuffer);
    glGenRenderbuffers(1, &hw_framedepthbuffer);

    glBindRenderbuffer(GL_RENDERBUFFER, hw_framedepthbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, 1, 1);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Configure framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, hw_framebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, hw_fb_texture.handle, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hw_framedepthbuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(Render_OpenGL, "Framebuffer setup failed, status %X", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
}

void RendererOpenGL::ReconfigureTexture(TextureInfo& texture, GPU::Regs::PixelFormat format, u32 width, u32 height) {
    GLint internal_format;

    texture.format = format;
    texture.width = width;
    texture.height = height;

    switch (format) {
    case GPU::Regs::PixelFormat::RGBA8:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_INT_8_8_8_8;
        break;

    case GPU::Regs::PixelFormat::RGB8:
        // This pixel format uses BGR since GL_UNSIGNED_BYTE specifies byte-order, unlike every
        // specific OpenGL type used in this function using native-endian (that is, little-endian
        // mostly everywhere) for words or half-words.
        // TODO: check how those behave on big-endian processors.
        internal_format = GL_RGB;
        texture.gl_format = GL_BGR;
        texture.gl_type = GL_UNSIGNED_BYTE;
        break;

    case GPU::Regs::PixelFormat::RGB565:
        internal_format = GL_RGB;
        texture.gl_format = GL_RGB;
        texture.gl_type = GL_UNSIGNED_SHORT_5_6_5;
        break;

    case GPU::Regs::PixelFormat::RGB5A1:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_5_5_5_1;
        break;

    case GPU::Regs::PixelFormat::RGBA4:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_4_4_4_4;
        break;

    default:
        UNIMPLEMENTED();
    }

    glBindTexture(GL_TEXTURE_2D, texture.handle);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, texture.width, texture.height, 0,
            texture.gl_format, texture.gl_type, nullptr);
}

void RendererOpenGL::ConfigureFramebufferTexture(TextureInfo& texture,
                                                 const GPU::Regs::FramebufferConfig& framebuffer) {
    ReconfigureTexture(texture, framebuffer.color_format, framebuffer.width, framebuffer.height);
}

/**
 * Draws a single texture to the emulator window, rotating the texture to correct for the 3DS's LCD rotation.
 */
void RendererOpenGL::DrawSingleScreenRotated(const TextureInfo& texture, float x, float y, float w, float h) {
    std::array<ScreenRectVertex, 4> vertices = {
        ScreenRectVertex(x,   y,   1.f, 0.f),
        ScreenRectVertex(x+w, y,   1.f, 1.f),
        ScreenRectVertex(x,   y+h, 0.f, 0.f),
        ScreenRectVertex(x+w, y+h, 0.f, 1.f),
    };

    glBindTexture(GL_TEXTURE_2D, texture.handle);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_handle);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/**
 * Draws the emulated screens to the emulator window.
 */
void RendererOpenGL::DrawScreens() {
    auto layout = render_window->GetFramebufferLayout();

    glViewport(0, 0, layout.width, layout.height);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(program_id);

    // Set projection matrix
    std::array<GLfloat, 3 * 2> ortho_matrix = MakeOrthographicMatrix((float)layout.width,
        (float)layout.height);
    glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());

    // Bind texture in Texture Unit 0
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uniform_color_texture, 0);

    DrawSingleScreenRotated(textures[0], (float)layout.top_screen.left, (float)layout.top_screen.top,
        (float)layout.top_screen.GetWidth(), (float)layout.top_screen.GetHeight());
    DrawSingleScreenRotated(textures[1], (float)layout.bottom_screen.left,(float)layout.bottom_screen.top,
        (float)layout.bottom_screen.GetWidth(), (float)layout.bottom_screen.GetHeight());

    m_current_frame++;
}

GLenum PICABlendFactorToOpenGL(u32 factor)
{
    switch (factor) {
    case Pica::registers.output_merger.alpha_blending.Zero:
        return GL_ZERO;
        break;

    case Pica::registers.output_merger.alpha_blending.One:
        return GL_ONE;
        break;

    case Pica::registers.output_merger.alpha_blending.SourceColor:
        return GL_SRC_COLOR;
        break;

    case Pica::registers.output_merger.alpha_blending.OneMinusSourceColor:
        return GL_ONE_MINUS_SRC_COLOR;
        break;

    case Pica::registers.output_merger.alpha_blending.DestColor:
        return GL_DST_COLOR;
        break;

    case Pica::registers.output_merger.alpha_blending.OneMinusDestColor:
        return GL_ONE_MINUS_DST_COLOR;
        break;

    case Pica::registers.output_merger.alpha_blending.SourceAlpha:
        return GL_SRC_ALPHA;
        break;

    case Pica::registers.output_merger.alpha_blending.OneMinusSourceAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
        break;

    case Pica::registers.output_merger.alpha_blending.DestAlpha:
        return GL_DST_ALPHA;
        break;

    case Pica::registers.output_merger.alpha_blending.OneMinusDestAlpha:
        return GL_ONE_MINUS_DST_ALPHA;
        break;

    case Pica::registers.output_merger.alpha_blending.ConstantColor:
        return GL_CONSTANT_COLOR;
        break;

    case Pica::registers.output_merger.alpha_blending.OneMinusConstantColor:
        return GL_ONE_MINUS_CONSTANT_COLOR;
        break;

    case Pica::registers.output_merger.alpha_blending.ConstantAlpha:
        return GL_CONSTANT_ALPHA;
        break;

    case Pica::registers.output_merger.alpha_blending.OneMinusConstantAlpha:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
        break;

    case Pica::registers.output_merger.alpha_blending.SourceAlphaSaturate:
        return GL_SRC_ALPHA_SATURATE;
        break;

    default:
        LOG_ERROR(Render_OpenGL, "Unknown blend factor %d", Pica::registers.output_merger.alpha_blending.factor_source_a.Value());
        return GL_ONE;
        break;
    }
}

void RendererOpenGL::CommitFramebuffer() {
    if (g_last_fb_color_addr != -1)
    {
        u32 bytes_per_pixel = GPU::Regs::BytesPerPixel(GPU::Regs::PixelFormat(g_last_fb_color_format));

        u8* ogl_img = new u8[hw_fb_texture.width * hw_fb_texture.height * bytes_per_pixel];

        glBindTexture(GL_TEXTURE_2D, hw_fb_texture.handle);
        glGetTexImage(GL_TEXTURE_2D, 0, hw_fb_texture.gl_format, hw_fb_texture.gl_type, ogl_img);
        glBindTexture(GL_TEXTURE_2D, 0);

        for (int x = 0; x < hw_fb_texture.width; ++x)
        {
            for (int y = 0; y < hw_fb_texture.height; ++y)
            {
                u8* color_buffer = Memory::GetPointer(Pica::PAddrToVAddr(g_last_fb_color_addr));

                // Similarly to textures, the render framebuffer is laid out from bottom to top, too.
                // NOTE: The framebuffer height register contains the actual FB height minus one.

                const u32 coarse_y = y & ~7;
                u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * hw_fb_texture.width * bytes_per_pixel;
                u32 ogl_px_idx = x * bytes_per_pixel + y * hw_fb_texture.width * bytes_per_pixel;

                switch (g_last_fb_color_format) {
                case Pica::registers.framebuffer.RGBA8:
                {
                    u8* pixel = color_buffer + dst_offset;
                    pixel[3] = ogl_img[ogl_px_idx + 3];
                    pixel[2] = ogl_img[ogl_px_idx + 2];
                    pixel[1] = ogl_img[ogl_px_idx + 1];
                    pixel[0] = ogl_img[ogl_px_idx];
                    break;
                }

                case Pica::registers.framebuffer.RGBA4:
                {
                    u8* pixel = color_buffer + dst_offset;
                    pixel[1] = (ogl_img[ogl_px_idx] & 0xF0) | (ogl_img[ogl_px_idx] >> 4);
                    pixel[0] = (ogl_img[ogl_px_idx + 1] & 0xF0) | (ogl_img[ogl_px_idx + 1] >> 4);
                    break;
                }

                default:
                    LOG_CRITICAL(Render_Software, "Unknown framebuffer color format %x", g_last_fb_color_format);
                    UNIMPLEMENTED();
                }
            }
        }

        delete ogl_img;

        // TODO: commit depth buffer to g_last_fb_depth_addr - still use morton?
    }
}

void RendererOpenGL::BeginBatch() {
    render_window->MakeCurrent();

    u32 cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 cur_fb_color_format = Pica::registers.framebuffer.color_format.Value();
    u32 cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    u32 cur_fb_depth_format = Pica::registers.framebuffer.depth_format;

    bool fb_switched = (g_last_fb_color_addr != cur_fb_color_addr || g_last_fb_depth_addr != cur_fb_depth_addr ||
                        g_last_fb_color_format != cur_fb_color_format || g_last_fb_depth_format != cur_fb_depth_format);
    bool fb_resized = (hw_fb_texture.width != Pica::registers.framebuffer.GetWidth() ||
                        hw_fb_texture.height != Pica::registers.framebuffer.GetHeight());
    bool fb_format_changed = (hw_fb_texture.format != (GPU::Regs::PixelFormat)Pica::registers.framebuffer.color_format.Value());

    if (fb_switched) {
        CommitFramebuffer();

        g_last_fb_color_addr = cur_fb_color_addr;
        g_last_fb_color_format = cur_fb_color_format;
        g_last_fb_depth_addr = cur_fb_depth_addr;
        g_last_fb_depth_format = cur_fb_depth_format;
    }

    if (fb_resized || fb_format_changed) {
        ReconfigureTexture(hw_fb_texture, (GPU::Regs::PixelFormat)Pica::registers.framebuffer.color_format.Value(), Pica::registers.framebuffer.GetWidth(), Pica::registers.framebuffer.GetHeight());
    }

    if (fb_resized) {
        glBindRenderbuffer(GL_RENDERBUFFER, hw_framedepthbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, Pica::registers.framebuffer.GetWidth(), Pica::registers.framebuffer.GetHeight());
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glViewport(0, 0, Pica::registers.framebuffer.GetWidth(), Pica::registers.framebuffer.GetHeight());
    }

    if (fb_switched) {
        // TODO: should actually read in color+depth buffers from fb in memory instead of clearing
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // TODO: Breaks oot since winding not consistent
    //switch (Pica::registers.cull_mode.Value()) {
    //case Pica::Regs::CullMode::KeepAll:
    //    glDisable(GL_CULL_FACE);
    //    break;
    //
    //case Pica::Regs::CullMode::KeepClockWise:
    //    glEnable(GL_CULL_FACE);
    //    glCullFace(GL_BACK);
    //    break;
    //
    //case Pica::Regs::CullMode::KeepCounterClockWise:
    //    glEnable(GL_CULL_FACE);
    //    glCullFace(GL_FRONT);
    //    break;
    //
    //default:
    //    LOG_ERROR(Render_OpenGL, "Unknown cull mode %d", Pica::registers.cull_mode.Value());
    //    break;
    //}

    if (Pica::registers.output_merger.depth_test_enable.Value()) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    switch (Pica::registers.output_merger.depth_test_func.Value()) {
    case Pica::registers.output_merger.Never:
        glDepthFunc(GL_NEVER);
        break;

    case Pica::registers.output_merger.Always:
        glDepthFunc(GL_ALWAYS);
        break;

    case Pica::registers.output_merger.Equal:
        glDepthFunc(GL_EQUAL);
        break;

    case Pica::registers.output_merger.NotEqual:
        glDepthFunc(GL_NOTEQUAL);
        break;

    case Pica::registers.output_merger.LessThan:
        glDepthFunc(GL_LESS);
        break;

    case Pica::registers.output_merger.LessThanOrEqual:
        glDepthFunc(GL_LEQUAL);
        break;

    case Pica::registers.output_merger.GreaterThan:
        glDepthFunc(GL_GREATER);
        break;

    case Pica::registers.output_merger.GreaterThanOrEqual:
        glDepthFunc(GL_GEQUAL);
        break;

    default:
        LOG_ERROR(Render_OpenGL, "Unknown depth test function %d", Pica::registers.output_merger.depth_test_func.Value());
        break;
    }

    // TODO: messes everything up
    //if (Pica::registers.output_merger.depth_write_enable.Value()) {
    //    glDepthMask(GL_TRUE);
    //} else {
    //    glDepthMask(GL_FALSE);
    //}

    if (Pica::registers.output_merger.alphablend_enable.Value()) {
        glEnable(GL_BLEND);

        glBlendColor(Pica::registers.output_merger.blend_const.r, Pica::registers.output_merger.blend_const.g, Pica::registers.output_merger.blend_const.b, Pica::registers.output_merger.blend_const.a);

        GLenum src_blend_rgb = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_source_rgb.Value());
        GLenum dst_blend_rgb = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_dest_rgb.Value());
        GLenum src_blend_a = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_source_a.Value());
        GLenum dst_blend_a = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_dest_a.Value());

        glBlendFuncSeparate(src_blend_rgb, dst_blend_rgb, src_blend_a, dst_blend_a);
    } else {
        glDisable(GL_BLEND);
    }

    auto tev_stages = Pica::registers.GetTevStages();
    for (int i = 0; i < 6; i++) {
        glUniform4iv(hw_uniform_tevs + i, 1, (GLint *)(&tev_stages[i]));
    }

    if (Pica::registers.output_merger.alpha_test.enable.Value()) {
        glUniform1i(hw_uniform_alphatest_func, Pica::registers.output_merger.alpha_test.func.Value());
        glUniform1f(hw_uniform_alphatest_ref, Pica::registers.output_merger.alpha_test.ref.Value() / 255.0f);
    } else {
        glUniform1i(hw_uniform_alphatest_func, 1);
    }

    auto pica_textures = Pica::registers.GetTextures();

    // Upload or use textures
    for (int i = 0; i < 3; ++i) {
        const auto& cur_texture = pica_textures[i];
        if (cur_texture.enabled) {
            u32 tex_paddr = cur_texture.config.GetPhysicalAddress();

            if (i == 0) {
                glActiveTexture(GL_TEXTURE0);
            } else if (i == 1) {
                glActiveTexture(GL_TEXTURE1);
            } else {
                glActiveTexture(GL_TEXTURE2);
            }

            std::map<u32, GLuint>::iterator cached_tex = g_tex_cache.find(tex_paddr);
            if (cached_tex != g_tex_cache.end()) {
                glBindTexture(GL_TEXTURE_2D, cached_tex->second);
            } else {
                GLuint new_tex_handle;
                glGenTextures(1, &new_tex_handle);
                glBindTexture(GL_TEXTURE_2D, new_tex_handle);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, cur_texture.config.wrap_s == Pica::Regs::TextureConfig::WrapMode::ClampToEdge ? GL_CLAMP_TO_EDGE : (cur_texture.config.wrap_s == Pica::Regs::TextureConfig::WrapMode::Repeat ? GL_REPEAT : GL_MIRRORED_REPEAT));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, cur_texture.config.wrap_t == Pica::Regs::TextureConfig::WrapMode::ClampToEdge ? GL_CLAMP_TO_EDGE : (cur_texture.config.wrap_t == Pica::Regs::TextureConfig::WrapMode::Repeat ? GL_REPEAT : GL_MIRRORED_REPEAT));

                Math::Vec4<u8>* rgba_tex = new Math::Vec4<u8>[cur_texture.config.width * cur_texture.config.height];

                auto info = Pica::DebugUtils::TextureInfo::FromPicaRegister(cur_texture.config, cur_texture.format);

                for (int i = 0; i < info.width; i++)
                {
                    for (int j = 0; j < info.height; j++)
                    {
                        rgba_tex[i + info.width * j] = Pica::DebugUtils::LookupTexture(Memory::GetPointer(Pica::PAddrToVAddr(tex_paddr)), i, info.height - 1 - j, info);
                    }
                }

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_tex);

                delete rgba_tex;

                g_tex_cache.insert(std::pair<u32, GLuint>(tex_paddr, new_tex_handle));
            }
        }
    }
}

void RendererOpenGL::DrawTriangle(const Pica::VertexShader::OutputVertex& v0, const Pica::VertexShader::OutputVertex& v1, const Pica::VertexShader::OutputVertex& v2) {
    g_vertex_batch.push_back(GameWorldVertex(v0));
    g_vertex_batch.push_back(GameWorldVertex(v1));
    g_vertex_batch.push_back(GameWorldVertex(v2));
}

void RendererOpenGL::EndBatch() {
    render_window->MakeCurrent();

    glBindBuffer(GL_ARRAY_BUFFER, hw_vertex_buffer_handle);
    glBufferData(GL_ARRAY_BUFFER, g_vertex_batch.size() * sizeof(GameWorldVertex), g_vertex_batch.data(), GL_STREAM_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, g_vertex_batch.size());
    g_vertex_batch.clear();
}

void RendererOpenGL::NotifyDMACopy(u32 dest, u32 size) {
    // Flush any texture that falls in the overwritten region
    // TODO: Should maintain size of tex and do actual check for region overlap, else assume that DMA always covers start address
    for (auto iter = g_tex_cache.begin(); iter != g_tex_cache.end();) {
        if ((u32)iter->first >= dest && (u32)iter->first <= dest + size) {
            glDeleteTextures(1, &iter->second);
            iter = g_tex_cache.erase(iter);
        } else {
            ++iter;
        }
    }
}

void RendererOpenGL::NotifyPreDisplayTransfer(u32 src, u32 dest)
{
    if (src == Pica::registers.framebuffer.GetColorBufferPhysicalAddress())
    {
        if (dest == GPU::g_regs.framebuffer_config[0].address_left1 ||
            dest == GPU::g_regs.framebuffer_config[0].address_left2 ||
            dest == GPU::g_regs.framebuffer_config[0].address_right1 ||
            dest == GPU::g_regs.framebuffer_config[0].address_right2) {
            // Copy fb to top screen tex
        }
        else if (dest == GPU::g_regs.framebuffer_config[1].address_left1 ||
            dest == GPU::g_regs.framebuffer_config[1].address_left2 ||
            dest == GPU::g_regs.framebuffer_config[1].address_right1 ||
            dest == GPU::g_regs.framebuffer_config[1].address_right2) {
            // Copy fb to bot screen tex
        }

        // TODO: Copy framebuffer to actual memory here

    }

    // RE: the above, can cache fb's and do a fast copy if it's trying to copy fb to region that LCD is pointing at
    // can circumvent the copy by not copying to mem and only to textures[0] or texture[1] (staying within ogl)
    // must also kill the mem->textures[0,1] upload in swapbuffers in that case
    // can also support HD this way, by keeping fb's in higher res

    CommitFramebuffer();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/// Updates the framerate
void RendererOpenGL::UpdateFramerate() {
}

/**
 * Set the emulator window to use for renderer
 * @param window EmuWindow handle to emulator window to use for rendering
 */
void RendererOpenGL::SetWindow(EmuWindow* window) {
    render_window = window;
}

/// Initialize the renderer
void RendererOpenGL::Init() {
    render_window->MakeCurrent();

    int err = ogl_LoadFunctions();
    if (ogl_LOAD_SUCCEEDED != err) {
        LOG_CRITICAL(Render_OpenGL, "Failed to initialize GL functions! Exiting...");
        exit(-1);
    }

    LOG_INFO(Render_OpenGL, "GL_VERSION: %s", glGetString(GL_VERSION));
    InitOpenGLObjects();
}

/// Shutdown the renderer
void RendererOpenGL::ShutDown() {
}
