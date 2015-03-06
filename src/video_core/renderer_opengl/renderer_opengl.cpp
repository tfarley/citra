// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/hw/gpu.h"
#include "core/mem_map.h"
#include "common/emu_window.h"
#include "video_core/video_core.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/renderer_opengl/gl_shaders.h"

#include "video_core/pica.h"
#include "video_core/shader_translator.h"
#include "video_core/vertex_shader.h"
#include "video_core/debug_utils/debug_utils.h"

#include <algorithm>

std::map<u32, GLuint> g_tex_cache;

GLuint g_cur_shader = -1;
u32 g_cur_shader_main = -1;
std::map<u32, GLuint> g_shader_cache;

std::vector<RawVertex> g_vertex_batch;

bool g_did_render;

u32 g_first_fb = -1;
u32 g_last_fb = -1;

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
#ifdef USE_OGL_RENDERER
    if (!g_did_render) {
        return;
    }

    g_did_render = 0;
#endif

    render_window->MakeCurrent();

    for(int i : {0, 1}) {
        const auto& framebuffer = GPU::g_regs.framebuffer_config[i];
		auto desired_size = GetDesiredFramebufferSize(textures[i], framebuffer);

		if (textures[i].width != (GLsizei)desired_size.x ||
			textures[i].height != (GLsizei)desired_size.y ||
            textures[i].format != framebuffer.color_format) {
            // Reallocate texture if the framebuffer size has changed.
            // This is expected to not happen very often and hence should not be a
            // performance problem.

            ConfigureFramebufferTexture(textures[i], framebuffer);
            ConfigureHWFramebuffer(i);
        }
#ifndef USE_OGL_RENDERER
        LoadFBToActiveGLTexture(GPU::g_regs.framebuffer_config[i], textures[i]);
#endif
    }

    glBindVertexArray(vertex_array_handle);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    DrawScreens();

    glBindVertexArray(hw_vertex_array_handle);
    glUseProgram(hw_program_id);

#ifdef USE_OGL_RENDERER
    // TODO: check if really needed
    //glFlush();
    //glFinish();
#endif

    // Swap buffers
    render_window->PollEvents();
    render_window->SwapBuffers();

#ifdef USE_OGL_RENDERER
    glBindFramebuffer(GL_FRAMEBUFFER, hw_framebuffers[0]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, hw_framebuffers[1]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
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
    glClearColor(1.0f, 1.0f, 1.0f, 0.0f);

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
    attrib_v = glGetAttribLocation(hw_program_id, "v");

    uniform_alphatest_func = glGetUniformLocation(hw_program_id, "alphatest_func");
    uniform_alphatest_ref = glGetUniformLocation(hw_program_id, "alphatest_ref");

    uniform_tex = glGetUniformLocation(hw_program_id, "tex");
    uniform_tevs = glGetUniformLocation(hw_program_id, "tevs");
    uniform_out_maps = glGetUniformLocation(hw_program_id, "out_maps");

    glUniform1i(uniform_tex, 0);
    glUniform1i(uniform_tex + 1, 1);
    glUniform1i(uniform_tex + 2, 2);

    glGenBuffers(1, &hw_vertex_buffer_handle);

    // Generate VAO
    glGenVertexArrays(1, &hw_vertex_array_handle);
    glBindVertexArray(hw_vertex_array_handle);

    // Attach vertex data to VAO
    glBindBuffer(GL_ARRAY_BUFFER, hw_vertex_buffer_handle);

    glUseProgram(hw_program_id);

    for (int i = 0; i < 8; i++) {
        glVertexAttribPointer(attrib_v + i, 4, GL_FLOAT, GL_FALSE, 8 * 4 * sizeof(float), (GLvoid*)(i * 4 * sizeof(float)));
        glEnableVertexAttribArray(attrib_v + i);
    }

    glGenFramebuffers(2, hw_framebuffers);
    glGenRenderbuffers(2, hw_framedepthbuffers);
}

Math::Vec2<u32> RendererOpenGL::GetDesiredFramebufferSize(TextureInfo& texture,
															const GPU::Regs::FramebufferConfig& framebuffer) {
#ifndef USE_OGL_HD
	return Math::Vec2<u32>(framebuffer.width, framebuffer.height);
#else
	auto viewport_extent = GetViewportExtent();
    Math::Vec2<u32> desired_size(viewport_extent.GetHeight() / 2, viewport_extent.GetWidth());

	if (texture.handle != textures[0].handle) {
		desired_size.x *= ((float)VideoCore::kScreenBottomWidth / (float)VideoCore::kScreenTopWidth);
		desired_size.y *= ((float)VideoCore::kScreenBottomHeight / (float)VideoCore::kScreenTopHeight);
	}

	return desired_size;
#endif
}

void RendererOpenGL::ConfigureFramebufferTexture(TextureInfo& texture,
                                                 const GPU::Regs::FramebufferConfig& framebuffer) {
    GPU::Regs::PixelFormat format = framebuffer.color_format;
    GLint internal_format;

    texture.format = format;
	auto desired_size = ((RendererOpenGL *)VideoCore::g_renderer)->GetDesiredFramebufferSize(texture, framebuffer);
	texture.width = desired_size.x;
	texture.height = desired_size.y;

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

void RendererOpenGL::ConfigureHWFramebuffer(int fb_index)
{
    // Set up depth buffer
    glBindRenderbuffer(GL_RENDERBUFFER, hw_framedepthbuffers[fb_index]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, textures[fb_index].width, textures[fb_index].height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    // Configure framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, hw_framebuffers[fb_index]);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textures[fb_index].handle, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hw_framedepthbuffers[fb_index]);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(Render_OpenGL, "Framebuffer setup failed, status %X", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
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
    auto viewport_extent = GetViewportExtent();
    glViewport(viewport_extent.left, viewport_extent.top, viewport_extent.GetWidth(), viewport_extent.GetHeight()); // TODO: Or bottom?
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(program_id);

    // Set projection matrix
    std::array<GLfloat, 3*2> ortho_matrix = MakeOrthographicMatrix((float)resolution_width, (float)resolution_height);
    glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());

    // Bind texture in Texture Unit 0
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uniform_color_texture, 0);

    const float max_width = std::max((float)VideoCore::kScreenTopWidth, (float)VideoCore::kScreenBottomWidth);
    const float top_x = 0.5f * (max_width - VideoCore::kScreenTopWidth);
    const float bottom_x = 0.5f * (max_width - VideoCore::kScreenBottomWidth);

    DrawSingleScreenRotated(textures[0], top_x, 0,
        (float)VideoCore::kScreenTopWidth, (float)VideoCore::kScreenTopHeight);
    DrawSingleScreenRotated(textures[1], bottom_x, (float)VideoCore::kScreenTopHeight,
        (float)VideoCore::kScreenBottomWidth, (float)VideoCore::kScreenBottomHeight);

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

void RendererOpenGL::BeginBatch() {
    render_window->MakeCurrent();

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
    } else {
        glDisable(GL_BLEND);
    }

    glBlendColor(Pica::registers.output_merger.blend_const.r, Pica::registers.output_merger.blend_const.g, Pica::registers.output_merger.blend_const.b, Pica::registers.output_merger.blend_const.a);

    GLenum src_blend_rgb = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_source_rgb.Value());
    GLenum dst_blend_rgb = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_dest_rgb.Value());
    GLenum src_blend_a = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_source_a.Value());
    GLenum dst_blend_a = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_dest_a.Value());

    glBlendFuncSeparate(src_blend_rgb, dst_blend_rgb, src_blend_a, dst_blend_a);

    // Uncomment to get shader translator output
    //FILE* outfile = fopen("shaderdecomp.txt", "w");
    //fwrite(PICABinToGLSL(Pica::VertexShader::GetShaderBinary().data(), Pica::VertexShader::GetSwizzlePatterns().data()).c_str(), PICABinToGLSL(Pica::VertexShader::GetShaderBinary().data(), Pica::VertexShader::GetSwizzlePatterns().data()).length(), 1, outfile);
    //fclose(outfile);
    //exit(0);
    
#ifdef USE_OGL_VTXSHADER
    // Switch shaders
    // TODO: Should never use g_vertex_shader_hw if using glsl shaders for rendering, but for now just switch every time
    //if (g_cur_shader_main != Pica::registers.vs_main_offset) {
        g_cur_shader_main = Pica::registers.vs_main_offset;

        std::map<u32, GLuint>::iterator cachedShader = g_shader_cache.find(Pica::registers.vs_main_offset);
        if (cachedShader != g_shader_cache.end()) {
            g_cur_shader = cachedShader->second;
        } else {
            g_cur_shader = ShaderUtil::LoadShaders(PICABinToGLSL(Pica::VertexShader::GetShaderBinary().data(), Pica::VertexShader::GetSwizzlePatterns().data()).c_str(), GLShaders::g_fragment_shader_hw);

            g_shader_cache.insert(std::pair<u32, GLuint>(Pica::registers.vs_main_offset, g_cur_shader));
        }

        glUseProgram(g_cur_shader);

        // TODO: probably a bunch of redundant stuff in here
        attrib_v = glGetAttribLocation(g_cur_shader, "v");

        uniform_c = glGetUniformLocation(g_cur_shader, "c");
        uniform_b = glGetUniformLocation(g_cur_shader, "b");
        uniform_i = glGetUniformLocation(g_cur_shader, "i");

        uniform_alphatest_func = glGetUniformLocation(g_cur_shader, "alphatest_func");
        uniform_alphatest_ref = glGetUniformLocation(g_cur_shader, "alphatest_ref");

        uniform_tex = glGetUniformLocation(g_cur_shader, "tex");
        uniform_tevs = glGetUniformLocation(g_cur_shader, "tevs");
        uniform_out_maps = glGetUniformLocation(g_cur_shader, "out_maps");

        glUniform1i(uniform_tex, 0);
        glUniform1i(uniform_tex + 1, 1);
        glUniform1i(uniform_tex + 2, 2);

        for (int i = 0; i < 8; i++) {
            glVertexAttribPointer(attrib_v + i, 4, GL_FLOAT, GL_FALSE, 8 * 4 * sizeof(float), (GLvoid*)(i * 4 * sizeof(float)));
            glEnableVertexAttribArray(attrib_v + i);
        }
    //}
#endif

    for (int i = 0; i < 7; ++i) {
        const auto& output_register_map = Pica::registers.vs_output_attributes[i];

        u32 semantics[4] = {
            output_register_map.map_x.Value(), output_register_map.map_y.Value(),
            output_register_map.map_z.Value(), output_register_map.map_w.Value()
        };

        // TODO: actually assign each component semantics, not just whole-vec4's
        // Also might only need to do this once per shader?
        if (output_register_map.map_x.Value() % 4 == 0) {
            glUniform1i(uniform_out_maps + output_register_map.map_x.Value() / 4, i);
        }

        //for (int comp = 0; comp < 4; ++comp)
        //    state.output_register_table[4 * i + comp] = ((float24*)&ret) + semantics[comp];
    }

    auto tev_stages = Pica::registers.GetTevStages();
    for (int i = 0; i < 6; i++) {
        glUniform4iv(uniform_tevs + i, 1, (GLint *)(&tev_stages[i]));
    }

    if (Pica::registers.output_merger.alpha_test.enable.Value()) {
        glUniform1i(uniform_alphatest_func, Pica::registers.output_merger.alpha_test.func.Value());
        glUniform1f(uniform_alphatest_ref, Pica::registers.output_merger.alpha_test.ref.Value() / 255.0f);
    } else {
        glUniform1i(uniform_alphatest_func, 1);
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

void RendererOpenGL::DrawTriangle(const RawVertex& v0, const RawVertex& v1, const RawVertex& v2) {
    g_vertex_batch.push_back(v0);
    g_vertex_batch.push_back(v1);
    g_vertex_batch.push_back(v2);
}

void RendererOpenGL::EndBatch() {
    render_window->MakeCurrent();

    u32 cur_fb = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();

    if (g_first_fb == -1) {
        // HACK: just keeps first fb addr to differentiate top/bot screens
        g_first_fb = cur_fb;
    }

    // TODO: reimplement when actual fb switch can be caught - currently every-frame hack for cave story resolution
    //if (g_last_fb != cur_fb) {
        int fbidx = cur_fb != g_first_fb;
        
        glBindFramebuffer(GL_FRAMEBUFFER, hw_framebuffers[fbidx]);
        
        glViewport(0, 0, textures[fbidx].width, textures[fbidx].height);

        g_last_fb = cur_fb;
    //}

    glBindBuffer(GL_ARRAY_BUFFER, hw_vertex_buffer_handle);
    glBufferData(GL_ARRAY_BUFFER, g_vertex_batch.size() * sizeof(RawVertex), g_vertex_batch.data(), GL_STREAM_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, g_vertex_batch.size());
    g_vertex_batch.clear();

    g_did_render = 1;
}

void RendererOpenGL::SetUniformBool(u32 index, int value) {
#ifdef USE_OGL_VTXSHADER
    render_window->MakeCurrent();
    glUniform1i(uniform_b + index, value);
#endif
}

void RendererOpenGL::SetUniformInts(u32 index, const u32* values) {
#ifdef USE_OGL_VTXSHADER
    render_window->MakeCurrent();
    glUniform4iv(uniform_i + index, 1, (const GLint*)values);
#endif
}

void RendererOpenGL::SetUniformFloats(u32 index, const float* values) {
#ifdef USE_OGL_VTXSHADER
    render_window->MakeCurrent();
    glUniform4fv(uniform_c + index, 1, values);
#endif
}

void RendererOpenGL::NotifyDMACopy(u32 address, u32 size) {
    // Flush any texture that falls in the overwritten region
    // TODO: Should maintain size of tex and do actual check for region overlap, else assume that DMA always covers start address
    for (auto iter = g_tex_cache.begin(); iter != g_tex_cache.end();) {
        if ((u32)iter->first >= address && (u32)iter->first <= address + size) {
            glDeleteTextures(1, &iter->second);
            iter = g_tex_cache.erase(iter);
        } else {
            ++iter;
        }
    }
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

MathUtil::Rectangle<unsigned> RendererOpenGL::GetViewportExtent() {
    unsigned framebuffer_width;
    unsigned framebuffer_height;
    std::tie(framebuffer_width, framebuffer_height) = render_window->GetFramebufferSize();

    float window_aspect_ratio = static_cast<float>(framebuffer_height) / framebuffer_width;
    float emulation_aspect_ratio = static_cast<float>(resolution_height) / resolution_width;

    MathUtil::Rectangle<unsigned> viewport_extent;
    if (window_aspect_ratio > emulation_aspect_ratio) {
        // Window is narrower than the emulation content => apply borders to the top and bottom
        unsigned viewport_height = static_cast<unsigned>(std::round(emulation_aspect_ratio * framebuffer_width));
        viewport_extent.left = 0;
        viewport_extent.top = (framebuffer_height - viewport_height) / 2;
        viewport_extent.right = viewport_extent.left + framebuffer_width;
        viewport_extent.bottom = viewport_extent.top + viewport_height;
    } else {
        // Otherwise, apply borders to the left and right sides of the window.
        unsigned viewport_width = static_cast<unsigned>(std::round(framebuffer_height / emulation_aspect_ratio));
        viewport_extent.left = (framebuffer_width - viewport_width) / 2;
        viewport_extent.top = 0;
        viewport_extent.right = viewport_extent.left + viewport_width;
        viewport_extent.bottom = viewport_extent.top + framebuffer_height;
    }

    return viewport_extent;
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
