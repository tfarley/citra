// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/settings.h"
#include "core/hw/gpu.h"

#include "video_core/color.h"
#include "video_core/pica.h"
#include "video_core/utils.h"
#include "video_core/renderer_opengl/gl_pica_to_gl.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_shaders.h"
#include "video_core/renderer_opengl/gl_shader_util.h"

#include "generated/gl_3_2_core.h"

#include <memory>

u32 ColorFormatBytesPerPixel(u32 format) {
    switch (format) {
    case Pica::registers.framebuffer.RGBA8:
        return 4;
    case Pica::registers.framebuffer.RGB8:
        return 3;
    case Pica::registers.framebuffer.RGB5A1:
    case Pica::registers.framebuffer.RGB565:
    case Pica::registers.framebuffer.RGBA4:
        return 2;
    default:
        UNIMPLEMENTED();
        break;
    }

    return 0;
}

RasterizerOpenGL::RasterizerOpenGL() : last_fb_color_addr(-1), last_fb_depth_addr(-1) {

}

RasterizerOpenGL::~RasterizerOpenGL() {
    // Set context for automatic resource destruction
    render_window->MakeCurrent();
}

/// Initialize API-specific GPU objects
void RasterizerOpenGL::InitObjects() {
    // Create the hardware shader program and get attrib/uniform locations
    shader.Create(GLShaders::g_vertex_shader_hw, GLShaders::g_fragment_shader_hw);
    attrib_position = glGetAttribLocation(shader.GetHandle(), "vert_position");
    attrib_color = glGetAttribLocation(shader.GetHandle(), "vert_color");
    attrib_texcoords = glGetAttribLocation(shader.GetHandle(), "vert_texcoords");

    uniform_alphatest_func = glGetUniformLocation(shader.GetHandle(), "alphatest_func");
    uniform_alphatest_ref = glGetUniformLocation(shader.GetHandle(), "alphatest_ref");

    uniform_tex = glGetUniformLocation(shader.GetHandle(), "tex");

    for (int i = 0; i < 6; i++) {
        auto& uniform_tev = uniform_tev_cfgs[i];

        std::string tev_ref_str = "tev_cfgs[" + std::to_string(i) + "]";
        uniform_tev.color_sources = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".color_sources").c_str());
        uniform_tev.alpha_sources = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".alpha_sources").c_str());
        uniform_tev.color_modifiers = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".color_modifiers").c_str());
        uniform_tev.alpha_modifiers = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".alpha_modifiers").c_str());
        uniform_tev.color_alpha_op = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".color_alpha_op").c_str());
        uniform_tev.color_alpha_multiplier = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".color_alpha_multiplier").c_str());
        uniform_tev.const_color = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".const_color").c_str());
        uniform_tev.updates_combiner_buffer_color_alpha = glGetUniformLocation(shader.GetHandle(), (tev_ref_str + ".updates_combiner_buffer_color_alpha").c_str());
    }

    uniform_out_maps = glGetUniformLocation(shader.GetHandle(), "out_maps");

    // Generate VBO and VAO
    vertex_buffer.Create();
    vertex_array.Create();

    // Update OpenGL state
    state.draw.vertex_array = vertex_array.GetHandle();
    state.draw.vertex_buffer = vertex_buffer.GetHandle();
    state.draw.shader_program = shader.GetHandle();

    for (int i = 0; i < 3; i++) {
        state.texture_unit[i].enabled_2d = true;
    }

    state.Apply();

    // Set the texture samplers to correspond to different texture units
    glUniform1i(uniform_tex, 0);
    glUniform1i(uniform_tex + 1, 1);
    glUniform1i(uniform_tex + 2, 2);

    // Set vertex attributes
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

    // Create textures for OGL framebuffer that will be rendered to, initially 1x1 to succeed in framebuffer creation
    fb_color_texture.texture.Create();
    ReconfigColorTexture(fb_color_texture, Pica::registers.framebuffer.RGBA8, 1, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    fb_depth_texture.texture.Create();
    ReconfigDepthTexture(fb_depth_texture, Pica::Regs::DepthFormat::D16, 1, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    // Configure OpenGL framebuffer
    framebuffer.Create();

    state.draw.framebuffer = framebuffer.GetHandle();
    state.texture_unit[0].enabled_2d = true;
    state.texture_unit[0].texture_2d = 0;
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_color_texture.texture.GetHandle(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb_depth_texture.texture.GetHandle(), 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(Render_OpenGL, "Framebuffer setup failed, status %X", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
}

/// Set the window (context) to draw with
void RasterizerOpenGL::SetWindow(EmuWindow* window) {
    render_window = window;
}

/// Converts the triangle verts to hardware data format and adds them to the current batch
void RasterizerOpenGL::AddTriangle(const Pica::VertexShader::OutputVertex& v0,
                                   const Pica::VertexShader::OutputVertex& v1,
                                   const Pica::VertexShader::OutputVertex& v2) {
    vertex_batch.push_back(HardwareVertex(v0));
    vertex_batch.push_back(HardwareVertex(v1));
    vertex_batch.push_back(HardwareVertex(v2));
}

/// Draw the current batch of triangles
void RasterizerOpenGL::DrawTriangles() {
    render_window->MakeCurrent();

    state.Apply();

    SyncFramebuffer();
    SyncDrawState();

    glBufferData(GL_ARRAY_BUFFER, vertex_batch.size() * sizeof(HardwareVertex), vertex_batch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_batch.size());

    vertex_batch.clear();
}

/// Notify rasterizer that a copy within 3ds memory will occur after this notification
void RasterizerOpenGL::NotifyPreCopy(u32 src_paddr, u32 size) {
    render_window->MakeCurrent();

    state.Apply();

    u32 cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 cur_fb_color_size = ColorFormatBytesPerPixel(Pica::registers.framebuffer.color_format.Value())
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    u32 cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    u32 cur_fb_depth_size = Pica::Regs::BytesPerDepthPixel(Pica::registers.framebuffer.depth_format)
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    // If source memory region overlaps 3ds framebuffers, commit them before the copy happens
    u32 max_lower = std::max(src_paddr, cur_fb_color_addr);
    u32 min_upper = std::min(src_paddr + size, cur_fb_color_addr + cur_fb_color_size);

    if (max_lower <= min_upper) {
        CommitFramebuffer();
    }

    max_lower = std::max(src_paddr, cur_fb_depth_addr);
    min_upper = std::min(src_paddr + size, cur_fb_depth_addr + cur_fb_depth_size);

    if (max_lower <= min_upper) {
        CommitFramebuffer();
    }
}

/// Notify rasterizer that a 3ds memory region has been changed
void RasterizerOpenGL::NotifyFlush(u32 paddr, u32 size) {
    render_window->MakeCurrent();

    state.Apply();

    u32 cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 cur_fb_color_size = ColorFormatBytesPerPixel(Pica::registers.framebuffer.color_format.Value())
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    u32 cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    u32 cur_fb_depth_size = Pica::Regs::BytesPerDepthPixel(Pica::registers.framebuffer.depth_format)
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    // If modified memory region overlaps 3ds framebuffers, reload their contents into OpenGL
    u32 max_lower = std::max(paddr, cur_fb_color_addr);
    u32 min_upper = std::min(paddr + size, cur_fb_color_addr + cur_fb_color_size);

    if (max_lower <= min_upper) {
        ReloadColorBuffer();
    }

    max_lower = std::max(paddr, cur_fb_depth_addr);
    min_upper = std::min(paddr + size, cur_fb_depth_addr + cur_fb_depth_size);

    if (max_lower <= min_upper) {
        ReloadDepthBuffer();
    }

    // Notify cache of flush in case the region touches a cached resource
    res_cache.NotifyFlush(paddr, size);
}

/// Reconfigure the OpenGL color texture to use the given format and dimensions
void RasterizerOpenGL::ReconfigColorTexture(TextureInfo& texture, u32 format, u32 width, u32 height) {
    GLint internal_format;

    texture.format = format;
    texture.width = width;
    texture.height = height;

    switch (format) {
    case Pica::registers.framebuffer.RGBA8:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_INT_8_8_8_8;
        break;

    case Pica::registers.framebuffer.RGB8:
        // This pixel format uses BGR since GL_UNSIGNED_BYTE specifies byte-order, unlike every
        // specific OpenGL type used in this function using native-endian (that is, little-endian
        // mostly everywhere) for words or half-words.
        // TODO: check how those behave on big-endian processors.
        internal_format = GL_RGB;
        texture.gl_format = GL_BGR;
        texture.gl_type = GL_UNSIGNED_BYTE;
        break;

    case Pica::registers.framebuffer.RGB5A1:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_5_5_5_1;
        break;

    case Pica::registers.framebuffer.RGB565:
        internal_format = GL_RGB;
        texture.gl_format = GL_RGB;
        texture.gl_type = GL_UNSIGNED_SHORT_5_6_5;
        break;

    case Pica::registers.framebuffer.RGBA4:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_4_4_4_4;
        break;

    default:
        UNIMPLEMENTED();
        break;
    }

    state.texture_unit[0].enabled_2d = true;
    state.texture_unit[0].texture_2d = texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, texture.width, texture.height, 0,
        texture.gl_format, texture.gl_type, nullptr);
}

/// Reconfigure the OpenGL depth texture to use the given format and dimensions
void RasterizerOpenGL::ReconfigDepthTexture(DepthTextureInfo& texture, Pica::Regs::DepthFormat format, u32 width, u32 height) {
    GLint internal_format;

    texture.format = format;
    texture.width = width;
    texture.height = height;

    switch (format) {
    case Pica::Regs::DepthFormat::D16:
        internal_format = GL_DEPTH_COMPONENT16;
        texture.gl_format = GL_DEPTH_COMPONENT;
        texture.gl_type = GL_UNSIGNED_SHORT;
        break;

    case Pica::Regs::DepthFormat::D24:
        internal_format = GL_DEPTH_COMPONENT24;
        texture.gl_format = GL_DEPTH_COMPONENT;
        texture.gl_type = GL_UNSIGNED_INT_24_8;
        break;

    case Pica::Regs::DepthFormat::D24S8:
        internal_format = GL_DEPTH24_STENCIL8;
        texture.gl_format = GL_DEPTH_STENCIL;
        texture.gl_type = GL_UNSIGNED_INT_24_8;
        break;

    default:
        UNIMPLEMENTED();
        break;
    }

    state.texture_unit[0].enabled_2d = true;
    state.texture_unit[0].texture_2d = texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, texture.width, texture.height, 0,
        texture.gl_format, texture.gl_type, nullptr);
}

/// Syncs the state and contents of the OpenGL framebuffer to match the current PICA framebuffer
void RasterizerOpenGL::SyncFramebuffer() {
    u32 cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 new_fb_color_format = Pica::registers.framebuffer.color_format.Value();

    u32 cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    Pica::Regs::DepthFormat new_fb_depth_format = Pica::registers.framebuffer.depth_format;

    bool fb_prop_changed = (fb_color_texture.format != new_fb_color_format ||
                            fb_depth_texture.format != new_fb_depth_format ||
                            fb_color_texture.width != Pica::registers.framebuffer.GetWidth() ||
                            fb_color_texture.height != Pica::registers.framebuffer.GetHeight());
    bool fb_modified = (last_fb_color_addr != cur_fb_color_addr ||
                        last_fb_depth_addr != cur_fb_depth_addr ||
                        fb_prop_changed);

    // Commit if fb modified in any way
    if (fb_modified) {
        CommitFramebuffer();
    }

    // Reconfigure framebuffer textures if any property has changed
    if (fb_prop_changed) {
        ReconfigColorTexture(fb_color_texture, new_fb_color_format,
                                Pica::registers.framebuffer.GetWidth(), Pica::registers.framebuffer.GetHeight());

        ReconfigDepthTexture(fb_depth_texture, new_fb_depth_format,
                                Pica::registers.framebuffer.GetWidth(), Pica::registers.framebuffer.GetHeight());

        // Only attach depth buffer as stencil if it supports stencil
        switch (new_fb_depth_format) {
        case Pica::Regs::DepthFormat::D16:
        case Pica::Regs::DepthFormat::D24:
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            break;

        case Pica::Regs::DepthFormat::D24S8:
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fb_depth_texture.texture.GetHandle(), 0);
            break;

        default:
            UNIMPLEMENTED();
            break;
        }
    }

    // Load buffer data again if fb modified in any way
    if (fb_modified) {
        last_fb_color_addr = cur_fb_color_addr;
        last_fb_depth_addr = cur_fb_depth_addr;

        // Currently not needed b/c of reloading buffers below, but will be needed for high-res rendering
        //glDepthMask(GL_TRUE);
        //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ReloadColorBuffer();
        ReloadDepthBuffer();
    }
}

/// Syncs the OpenGL drawing state to match the current PICA state
void RasterizerOpenGL::SyncDrawState() {
    // Sync the viewport
    GLsizei viewportWidth = (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_x.Value()).ToFloat32() * 2;
    GLsizei viewportHeight = (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_y.Value()).ToFloat32() * 2;

    // OpenGL uses different y coordinates, so negate corner offset and flip origin
    // TODO: Ensure viewport_corner.x should not be negated or origin flipped
    glViewport((GLsizei)static_cast<float>(Pica::registers.viewport_corner.x.Value()),
                -(GLsizei)static_cast<float>(Pica::registers.viewport_corner.y.Value())
                    + Pica::registers.framebuffer.GetHeight() - viewportHeight,
                viewportWidth, viewportHeight);

    // Sync the cull mode
    switch (Pica::registers.cull_mode.Value()) {
    case Pica::Regs::CullMode::KeepAll:
        state.cull.enabled = false;
        break;

    case Pica::Regs::CullMode::KeepClockWise:
        state.cull.enabled = true;
        state.cull.mode = GL_BACK;
        break;

    case Pica::Regs::CullMode::KeepCounterClockWise:
        state.cull.enabled = true;
        state.cull.mode = GL_FRONT;
        break;

    default:
        LOG_ERROR(Render_OpenGL, "Unknown cull mode %d", Pica::registers.cull_mode.Value());
        break;
    }

    // Sync depth test
    if (Pica::registers.output_merger.depth_test_enable.Value()) {
        state.depth.test_enabled = true;
        state.depth.test_func = PicaToGL::CompareFunc(Pica::registers.output_merger.depth_test_func.Value());
    } else {
        state.depth.test_enabled = false;
    }

    // Sync depth writing
    if (Pica::registers.output_merger.depth_write_enable.Value()) {
        state.depth.write_mask = GL_TRUE;
    } else {
        state.depth.write_mask = GL_FALSE;
    }

    // Sync stencil test
    // TODO: Untested, make sure stencil_reference_value refers to this mask
    if (Pica::registers.output_merger.stencil_test.stencil_test_enable.Value()) {
        state.stencil.test_enabled = true;
        state.stencil.test_func = PicaToGL::CompareFunc(Pica::registers.output_merger.stencil_test.stencil_test_func.Value());
        state.stencil.test_ref = Pica::registers.output_merger.stencil_test.stencil_reference_value.Value();
        state.stencil.test_mask = Pica::registers.output_merger.stencil_test.stencil_replacement_value.Value();
    }
    else {
        state.stencil.test_enabled = false;
    }

    // Sync stencil writing
    // TODO: Untested, make sure stencil_mask refers to this mask
    state.stencil.write_mask = Pica::registers.output_merger.stencil_test.stencil_mask.Value();

    // TODO: Need to sync glStencilOp() once corresponding PICA registers are discovered

    // Sync blend state
    if (Pica::registers.output_merger.alphablend_enable.Value()) {
        state.blend.enabled = true;

        state.blend.color.red = (GLclampf)Pica::registers.output_merger.blend_const.r.Value() / 255.0f;
        state.blend.color.green = (GLclampf)Pica::registers.output_merger.blend_const.g.Value() / 255.0f;
        state.blend.color.blue = (GLclampf)Pica::registers.output_merger.blend_const.b.Value() / 255.0f;
        state.blend.color.alpha = (GLclampf)Pica::registers.output_merger.blend_const.a.Value() / 255.0f;

        state.blend.src_rgb_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_source_rgb.Value());
        state.blend.dst_rgb_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_dest_rgb.Value());
        state.blend.src_a_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_source_a.Value());
        state.blend.dst_a_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_dest_a.Value());
    } else {
        state.blend.enabled = false;
    }

    // Sync bound texture(s), upload if uncached
    auto pica_textures = Pica::registers.GetTextures();

    for (int i = 0; i < 3; ++i) {
        const auto& texture = pica_textures[i];

        if (texture.enabled) {
            state.texture_unit[i].enabled_2d = true;
            res_cache.LoadAndBindTexture(state, i, texture);
        }
        else {
            state.texture_unit[i].enabled_2d = false;
        }
    }

    state.Apply();

    // Sync shader output register mapping to hw shader
    for (int i = 0; i < 7 * 4; ++i) {
        glUniform1i(uniform_out_maps + i, 0);
    }

    for (int i = 0; i < 6; ++i) {
        const auto& output_register_map = Pica::registers.vs_output_attributes[i];

        u32 semantics[4] = {
            output_register_map.map_x.Value(), output_register_map.map_y.Value(),
            output_register_map.map_z.Value(), output_register_map.map_w.Value()
        };

        // TODO: Might only need to do this once per shader? Not sure when/if out maps are modified.
        for (int comp = 0; comp < 4; ++comp) {
            if (semantics[comp] != Pica::Regs::VSOutputAttributes::INVALID) {
                glUniform1i(uniform_out_maps + semantics[comp], 4 * i + comp);
            }
        }
    }

    // Sync texture environment configurations to hw shader
    auto tev_stages = Pica::registers.GetTevStages();
    for (int i = 0; i < 6; i++) {
        const auto& stage = tev_stages[i];
        const auto& uniform_tev_cfg = uniform_tev_cfgs[i];

        GLint color_srcs[3] = { (GLint)stage.color_source1.Value(), (GLint)stage.color_source2.Value(), (GLint)stage.color_source3.Value() };
        GLint alpha_srcs[3] = { (GLint)stage.alpha_source1.Value(), (GLint)stage.alpha_source2.Value(), (GLint)stage.alpha_source3.Value() };
        GLint color_mods[3] = { (GLint)stage.color_modifier1.Value(), (GLint)stage.color_modifier2.Value(), (GLint)stage.color_modifier3.Value() };
        GLint alpha_mods[3] = { (GLint)stage.alpha_modifier1.Value(), (GLint)stage.alpha_modifier2.Value(), (GLint)stage.alpha_modifier3.Value() };
        GLfloat const_color[4] = { stage.const_r.Value() / 255.0f,
                                    stage.const_g.Value() / 255.0f,
                                    stage.const_b.Value() / 255.0f,
                                    stage.const_a.Value() / 255.0f };

        glUniform3iv(uniform_tev_cfg.color_sources, 1, color_srcs);
        glUniform3iv(uniform_tev_cfg.alpha_sources, 1, alpha_srcs);
        glUniform3iv(uniform_tev_cfg.color_modifiers, 1, color_mods);
        glUniform3iv(uniform_tev_cfg.alpha_modifiers, 1, alpha_mods);
        glUniform2i(uniform_tev_cfg.color_alpha_op, (GLint)stage.color_op.Value(), (GLint)stage.alpha_op.Value());
        glUniform2f(uniform_tev_cfg.color_alpha_multiplier, stage.GetColorMultiplier(), stage.GetAlphaMultiplier());
        glUniform4fv(uniform_tev_cfg.const_color, 1, const_color);
        glUniform2i(uniform_tev_cfg.updates_combiner_buffer_color_alpha,
                    Pica::registers.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferColor(i),
                    Pica::registers.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferAlpha(i));
    }

    // Sync alpha testing to hw shader
    if (Pica::registers.output_merger.alpha_test.enable.Value()) {
        glUniform1i(uniform_alphatest_func, Pica::registers.output_merger.alpha_test.func.Value());
        glUniform1f(uniform_alphatest_ref, Pica::registers.output_merger.alpha_test.ref.Value() / 255.0f);
    } else {
        glUniform1i(uniform_alphatest_func, 1);
    }
}

/// Copies the 3ds color framebuffer into the OpenGL color framebuffer texture
void RasterizerOpenGL::ReloadColorBuffer() {
    u8* color_buffer = Memory::GetPhysicalPointer(last_fb_color_addr);

    if (color_buffer == nullptr) {
        return;
    }

    u32 bytes_per_pixel = ColorFormatBytesPerPixel(fb_color_texture.format);

    u8* ogl_img = new u8[fb_color_texture.width * fb_color_texture.height * bytes_per_pixel];

    // TODO: Evaluate whether u16/memcpy/u32 is faster for 2/3/4 bpp versus memcpy for all
    for (int x = 0; x < fb_color_texture.width; ++x)
    {
        for (int y = 0; y < fb_color_texture.height; ++y)
        {
            const u32 coarse_y = y & ~7;
            u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_color_texture.width * bytes_per_pixel;
            u32 ogl_px_idx = x * bytes_per_pixel + y * fb_color_texture.width * bytes_per_pixel;

            u8* pixel = color_buffer + dst_offset;
            memcpy(&ogl_img[ogl_px_idx], pixel, bytes_per_pixel);
        }
    }

    state.texture_unit[0].enabled_2d = true;
    state.texture_unit[0].texture_2d = fb_color_texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_color_texture.width, fb_color_texture.height, fb_color_texture.gl_format, fb_color_texture.gl_type, ogl_img);

    delete[] ogl_img;
}

/// Copies the 3ds depth framebuffer into the OpenGL depth framebuffer texture
void RasterizerOpenGL::ReloadDepthBuffer() {
    // TODO: Appears to work, but double-check endianness of depth values and order of depth-stencil
    u8* depth_buffer = Memory::GetPhysicalPointer(last_fb_depth_addr);

    if (depth_buffer == nullptr) {
        return;
    }

    u32 bytes_per_pixel = Pica::Regs::BytesPerDepthPixel(fb_depth_texture.format);

    // OpenGL needs 4 bpp alignment for D24
    u32 ogl_bpp = bytes_per_pixel == 3 ? 4 : bytes_per_pixel;

    u8* ogl_img = new u8[fb_depth_texture.width * fb_depth_texture.height * ogl_bpp];

    for (int x = 0; x < fb_depth_texture.width; ++x)
    {
        for (int y = 0; y < fb_depth_texture.height; ++y)
        {
            const u32 coarse_y = y & ~7;
            u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_depth_texture.width * bytes_per_pixel;
            u32 ogl_px_idx = x + y * fb_depth_texture.width;

            switch (fb_depth_texture.format) {
            case Pica::Regs::DepthFormat::D16:
                ((u16*)ogl_img)[ogl_px_idx] = Color::DecodeD16(depth_buffer + dst_offset);
                break;
            case Pica::Regs::DepthFormat::D24:
                ((u32*)ogl_img)[ogl_px_idx] = Color::DecodeD24(depth_buffer + dst_offset);
                break;
            case Pica::Regs::DepthFormat::D24S8:
            {
                Math::Vec2<u32> depth_stencil = Color::DecodeD24S8(depth_buffer + dst_offset);
                ((u32*)ogl_img)[ogl_px_idx] = depth_stencil.x << 8 | depth_stencil.y;
                break;
            }
            default:
                LOG_CRITICAL(Render_OpenGL, "Unknown memory framebuffer depth format %x", fb_depth_texture.format);
                UNIMPLEMENTED();
                break;
            }
        }
    }

    state.texture_unit[0].enabled_2d = true;
    state.texture_unit[0].texture_2d = fb_depth_texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_depth_texture.width, fb_depth_texture.height, fb_depth_texture.gl_format, fb_depth_texture.gl_type, ogl_img);

    delete[] ogl_img;
}

/**
* Save the current OpenGL framebuffer to the current PICA framebuffer in 3ds memory
* Loads the OpenGL framebuffer textures into temporary buffers
* Then copies into the 3ds framebuffer using proper Morton order
*/
void RasterizerOpenGL::CommitFramebuffer() {
    if (last_fb_color_addr != -1)
    {
        u8* color_buffer = Memory::GetPhysicalPointer(last_fb_color_addr);

        if (color_buffer != nullptr) {
            u32 bytes_per_pixel = ColorFormatBytesPerPixel(fb_color_texture.format);

            std::unique_ptr<u8> ogl_img(new u8[fb_color_texture.width * fb_color_texture.height * bytes_per_pixel]);

            state.texture_unit[0].enabled_2d = true;
            state.texture_unit[0].texture_2d = fb_color_texture.texture.GetHandle();
            state.Apply();

            glActiveTexture(GL_TEXTURE0);
            glGetTexImage(GL_TEXTURE_2D, 0, fb_color_texture.gl_format, fb_color_texture.gl_type, ogl_img.get());

            for (int x = 0; x < fb_color_texture.width; ++x)
            {
                for (int y = 0; y < fb_color_texture.height; ++y)
                {
                    const u32 coarse_y = y & ~7;
                    u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_color_texture.width * bytes_per_pixel;
                    u32 ogl_px_idx = x * bytes_per_pixel + y * fb_color_texture.width * bytes_per_pixel;

                    u8* pixel = color_buffer + dst_offset;
                    memcpy(pixel, &ogl_img.get()[ogl_px_idx], bytes_per_pixel);
                }
            }
        }
    }

    if (last_fb_depth_addr != -1)
    {
        // TODO: Output seems correct visually, but doesn't quite match sw renderer output. One of them is wrong.
        u8* depth_buffer = Memory::GetPhysicalPointer(last_fb_depth_addr);

        if (depth_buffer != nullptr) {
            u32 bytes_per_pixel = Pica::Regs::BytesPerDepthPixel(fb_depth_texture.format);

            // OpenGL needs 4 bpp alignment for D24
            u32 ogl_bpp = bytes_per_pixel == 3 ? 4 : bytes_per_pixel;

            std::unique_ptr<u8> ogl_img(new u8[fb_depth_texture.width * fb_depth_texture.height * ogl_bpp]);

            state.texture_unit[0].enabled_2d = true;
            state.texture_unit[0].texture_2d = fb_depth_texture.texture.GetHandle();
            state.Apply();

            glActiveTexture(GL_TEXTURE0);
            glGetTexImage(GL_TEXTURE_2D, 0, fb_depth_texture.gl_format, fb_depth_texture.gl_type, ogl_img.get());

            for (int x = 0; x < fb_depth_texture.width; ++x)
            {
                for (int y = 0; y < fb_depth_texture.height; ++y)
                {
                    const u32 coarse_y = y & ~7;
                    u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_depth_texture.width * bytes_per_pixel;
                    u32 ogl_px_idx = x + y * fb_depth_texture.width;

                    switch (fb_depth_texture.format) {
                    case Pica::Regs::DepthFormat::D16:
                        Color::EncodeD16(((u16*)ogl_img.get())[ogl_px_idx], depth_buffer + dst_offset);
                        break;
                    case Pica::Regs::DepthFormat::D24:
                        Color::EncodeD24(((u32*)ogl_img.get())[ogl_px_idx], depth_buffer + dst_offset);
                        break;
                    case Pica::Regs::DepthFormat::D24S8:
                    {
                        u32 depth_stencil = ((u32*)ogl_img.get())[ogl_px_idx];
                        Color::EncodeD24S8(depth_stencil >> 8, depth_stencil & 0xFF, depth_buffer + dst_offset);
                        break;
                    }
                    default:
                        LOG_CRITICAL(Render_OpenGL, "Unknown framebuffer depth format %x", fb_depth_texture.format);
                        UNIMPLEMENTED();
                        break;
                    }
                }
            }
        }
    }
}
