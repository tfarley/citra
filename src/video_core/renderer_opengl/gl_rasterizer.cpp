// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/settings.h"
#include "core/hw/gpu.h"

#include "video_core/pica.h"
#include "video_core/vertex_processor.h"
#include "video_core/utils.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shaders.h"
#include "video_core/renderer_opengl/gl_shader_util.h"

#include "generated/gl_3_2_core.h"

/**
* Vertex structure that the hardware rendered vertices are composed of.
*/
struct HardwareVertex {
    HardwareVertex(const Pica::VertexShader::OutputVertex& v) {
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

std::vector<HardwareVertex> g_vertex_batch;

RasterizerOpenGL::RasterizerOpenGL(ResourceManagerOpenGL* res_mgr) : res_mgr(res_mgr), last_fb_color_addr(-1), last_fb_depth_addr(-1) {
    res_cache = new RasterizerCacheOpenGL(res_mgr);

    shader_handle = ShaderUtil::LoadShaders(GLShaders::g_vertex_shader_hw, GLShaders::g_fragment_shader_hw);
    attrib_position = glGetAttribLocation(shader_handle, "vert_position");
    attrib_color = glGetAttribLocation(shader_handle, "vert_color");
    attrib_texcoords = glGetAttribLocation(shader_handle, "vert_texcoords");

    uniform_alphatest_func = glGetUniformLocation(shader_handle, "alphatest_func");
    uniform_alphatest_ref = glGetUniformLocation(shader_handle, "alphatest_ref");

    uniform_tex = glGetUniformLocation(shader_handle, "tex");
    
    for (int i = 0; i < 6; i++) {
        std::string tev_ref_str = "tevs[" + std::to_string(i) + "]";
        uniform_tevs[i].color_src = glGetUniformLocation(shader_handle, (tev_ref_str + ".color_src").c_str());
        uniform_tevs[i].alpha_src = glGetUniformLocation(shader_handle, (tev_ref_str + ".alpha_src").c_str());
        uniform_tevs[i].color_mod = glGetUniformLocation(shader_handle, (tev_ref_str + ".color_mod").c_str());
        uniform_tevs[i].alpha_mod = glGetUniformLocation(shader_handle, (tev_ref_str + ".alpha_mod").c_str());
        uniform_tevs[i].color_op = glGetUniformLocation(shader_handle, (tev_ref_str + ".color_op").c_str());
        uniform_tevs[i].alpha_op = glGetUniformLocation(shader_handle, (tev_ref_str + ".alpha_op").c_str());
        uniform_tevs[i].const_color = glGetUniformLocation(shader_handle, (tev_ref_str + ".const_color").c_str());
    }

    uniform_out_maps = glGetUniformLocation(shader_handle, "out_maps");

    glUniform1i(uniform_tex, 0);
    glUniform1i(uniform_tex + 1, 1);
    glUniform1i(uniform_tex + 2, 2);

    vertex_buffer_handle = res_mgr->NewBuffer();

    // Generate VAO
    vertex_array_handle = res_mgr->NewVAO();
    glBindVertexArray(vertex_array_handle);

    // Attach vertex data to VAO
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_handle);

    glUseProgram(shader_handle);

    glVertexAttribPointer(attrib_position, 4, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, position));
    glVertexAttribPointer(attrib_color, 4, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, color));
    glVertexAttribPointer(attrib_texcoords, 2, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord0));
    glVertexAttribPointer(attrib_texcoords + 1, 2, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord1));
    glVertexAttribPointer(attrib_texcoords + 2, 2, GL_FLOAT, GL_FALSE, sizeof(HardwareVertex), (GLvoid*)offsetof(HardwareVertex, tex_coord2));
    glEnableVertexAttribArray(attrib_position);
    glEnableVertexAttribArray(attrib_color);
    glEnableVertexAttribArray(attrib_texcoords);
    glEnableVertexAttribArray(attrib_texcoords + 1);
    glEnableVertexAttribArray(attrib_texcoords + 2);

    fb_color_texture.format = (GPU::Regs::PixelFormat)0;
    fb_color_texture.width = 1;
    fb_color_texture.height = 1;
    fb_color_texture.gl_format = GL_RGBA;
    fb_color_texture.gl_type = GL_UNSIGNED_BYTE;

    fb_color_texture.handle = res_mgr->NewTexture();

    glBindTexture(GL_TEXTURE_2D, fb_color_texture.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, fb_color_texture.gl_format, fb_color_texture.width, fb_color_texture.height, 0, fb_color_texture.gl_format, fb_color_texture.gl_type, nullptr);

    fb_depth_texture.format = (GPU::Regs::PixelFormat)0;
    fb_depth_texture.width = 1;
    fb_depth_texture.height = 1;
    fb_depth_texture.gl_format = GL_DEPTH_COMPONENT;
    fb_depth_texture.gl_type = GL_FLOAT;

    fb_depth_texture.handle = res_mgr->NewTexture();

    glBindTexture(GL_TEXTURE_2D, fb_depth_texture.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glTexImage2D(GL_TEXTURE_2D, 0, fb_depth_texture.gl_format, fb_depth_texture.width, fb_depth_texture.height, 0, fb_depth_texture.gl_format, fb_depth_texture.gl_type, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);

    framebuffer_handle = res_mgr->NewFramebuffer();

    // Configure framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_handle);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_color_texture.handle, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb_depth_texture.handle, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(Render_OpenGL, "Framebuffer setup failed, status %X", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
}

RasterizerOpenGL::~RasterizerOpenGL() {
    res_mgr->DeleteBuffer(vertex_buffer_handle);
    res_mgr->DeleteVAO(vertex_array_handle);

    res_mgr->DeleteTexture(fb_color_texture.handle);
    res_mgr->DeleteTexture(fb_depth_texture.handle);

    res_mgr->DeleteFramebuffer(fb_depth_texture.handle);

    delete res_cache;
}

void ProcessTriangle(Pica::VertexShader::OutputVertex& v0, Pica::VertexShader::OutputVertex& v1, Pica::VertexShader::OutputVertex& v2) {
    g_vertex_batch.push_back(HardwareVertex(v0));
    g_vertex_batch.push_back(HardwareVertex(v1));
    g_vertex_batch.push_back(HardwareVertex(v2));
}

void RasterizerOpenGL::ReconfigureTexture(TextureInfo& texture, GPU::Regs::PixelFormat format, u32 width, u32 height) {
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

/// Draw a batch of triangles
void RasterizerOpenGL::DrawBatch(bool is_indexed) {
    if (needs_state_reinit) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_handle);
        glBindVertexArray(vertex_array_handle);
        glUseProgram(shader_handle);

        needs_state_reinit = false;
    }

    u32 cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 cur_fb_color_format = Pica::registers.framebuffer.color_format.Value();
    u32 cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    u32 cur_fb_depth_format = Pica::registers.framebuffer.depth_format;

    bool fb_switched = (last_fb_color_addr != cur_fb_color_addr || last_fb_depth_addr != cur_fb_depth_addr ||
                        last_fb_color_format != cur_fb_color_format || last_fb_depth_format != cur_fb_depth_format);
    bool fb_resized = (fb_color_texture.width != Pica::registers.framebuffer.GetWidth() ||
                        fb_color_texture.height != Pica::registers.framebuffer.GetHeight());
    bool fb_format_changed = (fb_color_texture.format != (GPU::Regs::PixelFormat)Pica::registers.framebuffer.color_format.Value() ||
                                fb_depth_texture.format != (GPU::Regs::PixelFormat)Pica::registers.framebuffer.depth_format);

    if (fb_switched) {
        CommitFramebuffer();
    }

    if (fb_resized || fb_format_changed) {
        ReconfigureTexture(fb_color_texture, (GPU::Regs::PixelFormat)Pica::registers.framebuffer.color_format.Value(), Pica::registers.framebuffer.GetWidth(), Pica::registers.framebuffer.GetHeight());

        fb_depth_texture.format = (GPU::Regs::PixelFormat)Pica::registers.framebuffer.depth_format;
        fb_depth_texture.width = Pica::registers.framebuffer.GetWidth();
        fb_depth_texture.height = Pica::registers.framebuffer.GetHeight();
        glBindTexture(GL_TEXTURE_2D, fb_depth_texture.handle);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, fb_depth_texture.width, fb_depth_texture.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    }

    // HACK: Need the offset here to correct some games' bottom screen offset. Problem is probably elsewhere though.
    glViewport((GLsizei)static_cast<float>(Pica::registers.viewport_corner.x.Value()),
                (GLsizei)static_cast<float>(Pica::registers.viewport_corner.y.Value())
                    + (Pica::registers.framebuffer.GetHeight() - (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_y.Value()).ToFloat32() * 2),
                (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_x.Value()).ToFloat32() * 2,
                (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_y.Value()).ToFloat32() * 2);

    if (fb_switched) {
        last_fb_color_addr = cur_fb_color_addr;
        last_fb_color_format = cur_fb_color_format;
        last_fb_depth_addr = cur_fb_depth_addr;
        last_fb_depth_format = cur_fb_depth_format;

        // Currently not needed b/c of reloading buffers below, but will be needed for hw vtx shaders
        //glDepthMask(GL_TRUE);
        //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ReloadColorBuffer();
        ReloadDepthBuffer();
    }

    SetupDrawState();

    if (Settings::values.gfx_use_hw_shaders == false) {
        Pica::VertexProcessor::ProcessBatch(is_indexed, ProcessTriangle);

        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_handle);
        glBufferData(GL_ARRAY_BUFFER, g_vertex_batch.size() * sizeof(HardwareVertex), g_vertex_batch.data(), GL_STREAM_DRAW);

        glDrawArrays(GL_TRIANGLES, 0, g_vertex_batch.size());
    }
    else {
        // TODO: Hardware rendering with hardware shaders
    }

    g_vertex_batch.clear();

    // Restore state to not mess up renderer
    glDisable(GL_DEPTH_TEST);
}

void RasterizerOpenGL::NotifySwapBuffers() {
    needs_state_reinit = true;

    CommitFramebuffer();
}

/// Notify renderer that memory region has been changed
void RasterizerOpenGL::NotifyFlush(bool is_phys_addr, u32 addr, u32 size) {
    if (is_phys_addr && Pica::registers.framebuffer.GetColorBufferPhysicalAddress() >= addr && Pica::registers.framebuffer.GetColorBufferPhysicalAddress() < addr + size) {
        ReloadColorBuffer();
    }
    else if (is_phys_addr && Pica::registers.framebuffer.GetDepthBufferPhysicalAddress() >= addr && Pica::registers.framebuffer.GetDepthBufferPhysicalAddress() < addr + size) {
        ReloadDepthBuffer();
    }

    res_cache->NotifyFlush(is_phys_addr, addr, size);
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

/// Set OpenGL state to correspond to PICA register states
void RasterizerOpenGL::SetupDrawState() {
    switch (Pica::registers.cull_mode.Value()) {
    case Pica::Regs::CullMode::KeepAll:
        glDisable(GL_CULL_FACE);
        break;

    case Pica::Regs::CullMode::KeepClockWise:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        break;

    case Pica::Regs::CullMode::KeepCounterClockWise:
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        break;

    default:
        LOG_ERROR(Render_OpenGL, "Unknown cull mode %d", Pica::registers.cull_mode.Value());
        break;
    }

    if (Pica::registers.output_merger.depth_test_enable.Value()) {
        glEnable(GL_DEPTH_TEST);
    }
    else {
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

    if (Pica::registers.output_merger.depth_write_enable.Value()) {
        glDepthMask(GL_TRUE);
    }
    else {
        glDepthMask(GL_FALSE);
    }

    if (Pica::registers.output_merger.alphablend_enable.Value()) {
        glEnable(GL_BLEND);

        glBlendColor(Pica::registers.output_merger.blend_const.r, Pica::registers.output_merger.blend_const.g, Pica::registers.output_merger.blend_const.b, Pica::registers.output_merger.blend_const.a);

        GLenum src_blend_rgb = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_source_rgb.Value());
        GLenum dst_blend_rgb = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_dest_rgb.Value());
        GLenum src_blend_a = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_source_a.Value());
        GLenum dst_blend_a = PICABlendFactorToOpenGL(Pica::registers.output_merger.alpha_blending.factor_dest_a.Value());

        glBlendFuncSeparate(src_blend_rgb, dst_blend_rgb, src_blend_a, dst_blend_a);
    }
    else {
        glDisable(GL_BLEND);
    }

    for (int i = 0; i < 16; ++i) {
        const auto& output_register_map = Pica::registers.vs_output_attributes[i];

        u32 semantics[4] = {
            output_register_map.map_x.Value(), output_register_map.map_y.Value(),
            output_register_map.map_z.Value(), output_register_map.map_w.Value()
        };

        // TODO: Might only need to do this once per shader?
        for (int comp = 0; comp < 4; ++comp) {
            glUniform1i(uniform_out_maps + semantics[comp], 4 * i + comp);
        }
    }

    auto tev_stages = Pica::registers.GetTevStages();
    for (int i = 0; i < 6; i++) {
        int color_srcs[3] = { (int)tev_stages[i].color_source1.Value(), (int)tev_stages[i].color_source2.Value(), (int)tev_stages[i].color_source3.Value() };
        int alpha_srcs[3] = { (int)tev_stages[i].alpha_source1.Value(), (int)tev_stages[i].alpha_source2.Value(), (int)tev_stages[i].alpha_source3.Value() };
        int color_mods[3] = { (int)tev_stages[i].color_modifier1.Value(), (int)tev_stages[i].color_modifier2.Value(), (int)tev_stages[i].color_modifier3.Value() };
        int alpha_mods[3] = { (int)tev_stages[i].alpha_modifier1.Value(), (int)tev_stages[i].alpha_modifier2.Value(), (int)tev_stages[i].alpha_modifier3.Value() };
        float const_color[4] = { tev_stages[i].const_r.Value() / 255.0f, tev_stages[i].const_g.Value() / 255.0f, tev_stages[i].const_b.Value() / 255.0f, tev_stages[i].const_a.Value() / 255.0f };

        glUniform3iv(uniform_tevs[i].color_src, 1, (GLint *)color_srcs);
        glUniform3iv(uniform_tevs[i].alpha_src, 1, (GLint *)alpha_srcs);
        glUniform3iv(uniform_tevs[i].color_mod, 1, (GLint *)color_mods);
        glUniform3iv(uniform_tevs[i].alpha_mod, 1, (GLint *)alpha_mods);
        glUniform1i(uniform_tevs[i].color_op, (int)tev_stages[i].color_op.Value());
        glUniform1i(uniform_tevs[i].alpha_op, (int)tev_stages[i].alpha_op.Value());
        glUniform4fv(uniform_tevs[i].const_color, 1, (GLfloat *)const_color);
    }

    if (Pica::registers.output_merger.alpha_test.enable.Value()) {
        glUniform1i(uniform_alphatest_func, Pica::registers.output_merger.alpha_test.func.Value());
        glUniform1f(uniform_alphatest_ref, Pica::registers.output_merger.alpha_test.ref.Value() / 255.0f);
    }
    else {
        glUniform1i(uniform_alphatest_func, 1);
    }

    auto pica_textures = Pica::registers.GetTextures();

    // Upload or use textures
    for (int i = 0; i < 3; ++i) {
        const auto& cur_texture = pica_textures[i];
        if (cur_texture.enabled) {
            if (i == 0) {
                glActiveTexture(GL_TEXTURE0);
            }
            else if (i == 1) {
                glActiveTexture(GL_TEXTURE1);
            }
            else {
                glActiveTexture(GL_TEXTURE2);
            }

            res_cache->LoadAndBindTexture(pica_textures[i]);
        }
    }
}

void RasterizerOpenGL::CommitFramebuffer() {
    if (last_fb_color_addr != -1)
    {
        u8* color_buffer = Memory::GetPointer(Pica::PAddrToVAddr(last_fb_color_addr));

        if (color_buffer != nullptr) {
            u32 bytes_per_pixel = GPU::Regs::BytesPerPixel(GPU::Regs::PixelFormat(last_fb_color_format));

            u8* ogl_img = new u8[fb_color_texture.width * fb_color_texture.height * bytes_per_pixel];

            glBindTexture(GL_TEXTURE_2D, fb_color_texture.handle);
            glGetTexImage(GL_TEXTURE_2D, 0, fb_color_texture.gl_format, fb_color_texture.gl_type, ogl_img);
            glBindTexture(GL_TEXTURE_2D, 0);

            for (int x = 0; x < fb_color_texture.width; ++x)
            {
                for (int y = 0; y < fb_color_texture.height; ++y)
                {
                    const u32 coarse_y = y & ~7;
                    u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_color_texture.width * bytes_per_pixel;
                    u32 ogl_px_idx = x * bytes_per_pixel + y * fb_color_texture.width * bytes_per_pixel;

                    switch (last_fb_color_format) {
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
                        LOG_CRITICAL(Render_Software, "Unknown framebuffer color format %x", last_fb_color_format);
                        UNIMPLEMENTED();
                    }
                }
            }

            delete ogl_img;
        }
    }

    if (last_fb_depth_addr != -1)
    {
        // TODO: Untested
        u8* depth_buffer = Memory::GetPointer(Pica::PAddrToVAddr(last_fb_depth_addr));

        if (depth_buffer != nullptr) {
            u32 bytes_per_pixel = GPU::Regs::BytesPerPixel(GPU::Regs::PixelFormat(last_fb_depth_format));

            float* ogl_img = new float[fb_depth_texture.width * fb_depth_texture.height];

            glBindTexture(GL_TEXTURE_2D, fb_depth_texture.handle);
            glGetTexImage(GL_TEXTURE_2D, 0, fb_depth_texture.gl_format, fb_depth_texture.gl_type, ogl_img);
            glBindTexture(GL_TEXTURE_2D, 0);

            for (int x = 0; x < fb_depth_texture.width; ++x)
            {
                for (int y = 0; y < fb_depth_texture.height; ++y)
                {
                    const u32 coarse_y = y & ~7;
                    u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_depth_texture.width * bytes_per_pixel;
                    u32 ogl_px_idx = x + y * fb_depth_texture.width;

                    u16_le* pixel = (u16_le*)(depth_buffer + dst_offset);
                    *pixel = ogl_img[ogl_px_idx];
                }
            }

            delete ogl_img;
        }
    }
}

void RasterizerOpenGL::ReloadColorBuffer() {
    u8* color_buffer = Memory::GetPointer(Pica::PAddrToVAddr(last_fb_color_addr));

    if (color_buffer != nullptr) {
        u32 bytes_per_pixel = GPU::Regs::BytesPerPixel(GPU::Regs::PixelFormat(last_fb_color_format));

        u8* ogl_img = new u8[fb_color_texture.width * fb_color_texture.height * bytes_per_pixel];

        for (int x = 0; x < fb_color_texture.width; ++x)
        {
            for (int y = 0; y < fb_color_texture.height; ++y)
            {
                const u32 coarse_y = y & ~7;
                u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_color_texture.width * bytes_per_pixel;
                u32 ogl_px_idx = x * bytes_per_pixel + y * fb_color_texture.width * bytes_per_pixel;

                switch (last_fb_color_format) {
                case Pica::registers.framebuffer.RGBA8:
                {
                    u8* pixel = color_buffer + dst_offset;
                    ogl_img[ogl_px_idx + 3] = pixel[3];
                    ogl_img[ogl_px_idx + 2] = pixel[2];
                    ogl_img[ogl_px_idx + 1] = pixel[1];
                    ogl_img[ogl_px_idx] = pixel[0];
                    break;
                }

                case Pica::registers.framebuffer.RGBA4:
                {
                    u8* pixel = color_buffer + dst_offset;
                    ogl_img[ogl_px_idx] = (pixel[1] & 0xF0) | (pixel[1] >> 4);
                    ogl_img[ogl_px_idx + 1] = (pixel[0] & 0xF0) | (pixel[0] >> 4);
                    break;
                }

                default:
                    LOG_CRITICAL(Render_Software, "Unknown memory framebuffer color format %x", last_fb_color_format);
                    UNIMPLEMENTED();
                }
            }
        }

        glBindTexture(GL_TEXTURE_2D, fb_color_texture.handle);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_color_texture.width, fb_color_texture.height, fb_color_texture.gl_format, fb_color_texture.gl_type, ogl_img);
        glBindTexture(GL_TEXTURE_2D, 0);

        delete ogl_img;
    }
}

void RasterizerOpenGL::ReloadDepthBuffer() {
    u8* depth_buffer = Memory::GetPointer(Pica::PAddrToVAddr(last_fb_depth_addr));

    if (depth_buffer != nullptr) {
        u32 bytes_per_pixel = GPU::Regs::BytesPerPixel(GPU::Regs::PixelFormat(last_fb_depth_format));

        float* ogl_img = new float[fb_depth_texture.width * fb_depth_texture.height];

        for (int x = 0; x < fb_depth_texture.width; ++x)
        {
            for (int y = 0; y < fb_depth_texture.height; ++y)
            {
                const u32 coarse_y = y & ~7;
                u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_depth_texture.width * bytes_per_pixel;
                u32 ogl_px_idx = x + y * fb_depth_texture.width;

                // TODO: Make sure this straight copy is correct
                u16_le* pixel = (u16_le*)(depth_buffer + dst_offset);
                ogl_img[ogl_px_idx] = *pixel;
            }
        }

        glBindTexture(GL_TEXTURE_2D, fb_depth_texture.handle);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_depth_texture.width, fb_depth_texture.height, fb_depth_texture.gl_format, fb_depth_texture.gl_type, ogl_img);
        glBindTexture(GL_TEXTURE_2D, 0);

        delete ogl_img;
    }
}
