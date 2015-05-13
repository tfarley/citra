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
        LOG_CRITICAL(Render_OpenGL, "Unknown framebuffer color format %x", format);
        UNIMPLEMENTED();
        break;
    }

    return 0;
}

RasterizerOpenGL::RasterizerOpenGL() : last_fb_color_addr(0), last_fb_depth_addr(0) {

}

RasterizerOpenGL::~RasterizerOpenGL() {
    // Set context for automatic resource destruction
    render_window->MakeCurrent();
}

void RasterizerOpenGL::InitObjects() {
    // Create the hardware shader program and get attrib/uniform locations
    shader.Create(GLShaders::g_vertex_shader_hw, GLShaders::g_fragment_shader_hw);
    attrib_position = glGetAttribLocation(shader.GetHandle(), "vert_position");
    attrib_color = glGetAttribLocation(shader.GetHandle(), "vert_color");
    attrib_texcoords = glGetAttribLocation(shader.GetHandle(), "vert_texcoords");

    uniform_alphatest_func = glGetUniformLocation(shader.GetHandle(), "alphatest_func");
    uniform_alphatest_ref = glGetUniformLocation(shader.GetHandle(), "alphatest_ref");

    uniform_tex = glGetUniformLocation(shader.GetHandle(), "tex");

    uniform_tev_combiner_buffer_color = glGetUniformLocation(shader.GetHandle(), "tev_combiner_buffer_color");

    for (int i = 0; i < 6; ++i) {
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

    for (auto& texture_unit : state.texture_units) {
        texture_unit.enabled_2d = true;
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
    state.texture_units[0].enabled_2d = true;
    state.texture_units[0].texture_2d = 0;
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb_color_texture.texture.GetHandle(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fb_depth_texture.texture.GetHandle(), 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG_CRITICAL(Render_OpenGL, "Framebuffer setup failed, status %X", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
}

void RasterizerOpenGL::SetWindow(EmuWindow* window) {
    render_window = window;
}

void RasterizerOpenGL::AddTriangle(const Pica::VertexShader::OutputVertex& v0,
                                   const Pica::VertexShader::OutputVertex& v1,
                                   const Pica::VertexShader::OutputVertex& v2) {
    vertex_batch.push_back(HardwareVertex(v0));
    vertex_batch.push_back(HardwareVertex(v1));
    vertex_batch.push_back(HardwareVertex(v2));
}

void RasterizerOpenGL::DrawTriangles() {
    render_window->MakeCurrent();

    state.Apply();

    SyncFramebuffer();
    SyncDrawState();

    glBufferData(GL_ARRAY_BUFFER, vertex_batch.size() * sizeof(HardwareVertex), vertex_batch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_batch.size());

    vertex_batch.clear();
}

void RasterizerOpenGL::NotifyPreCopy(PAddr src_addr, u32 size) {
    render_window->MakeCurrent();

    state.Apply();

    PAddr cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 cur_fb_color_size = ColorFormatBytesPerPixel(Pica::registers.framebuffer.color_format)
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    PAddr cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    u32 cur_fb_depth_size = Pica::Regs::BytesPerDepthPixel(Pica::registers.framebuffer.depth_format)
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    // If source memory region overlaps 3DS framebuffers, commit them before the copy happens
    PAddr max_low_addr_bound = std::max(src_addr, cur_fb_color_addr);
    PAddr min_hi_addr_bound = std::min(src_addr + size, cur_fb_color_addr + cur_fb_color_size);

    if (max_low_addr_bound <= min_hi_addr_bound) {
        CommitFramebuffer();
    }

    max_low_addr_bound = std::max(src_addr, cur_fb_depth_addr);
    min_hi_addr_bound = std::min(src_addr + size, cur_fb_depth_addr + cur_fb_depth_size);

    if (max_low_addr_bound <= min_hi_addr_bound) {
        CommitFramebuffer();
    }
}

void RasterizerOpenGL::NotifyFlush(PAddr addr, u32 size) {
    render_window->MakeCurrent();

    state.Apply();

    PAddr cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 cur_fb_color_size = ColorFormatBytesPerPixel(Pica::registers.framebuffer.color_format)
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    PAddr cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
    u32 cur_fb_depth_size = Pica::Regs::BytesPerDepthPixel(Pica::registers.framebuffer.depth_format)
                            * Pica::registers.framebuffer.GetWidth() * Pica::registers.framebuffer.GetHeight();

    // If modified memory region overlaps 3DS framebuffers, reload their contents into OpenGL
    PAddr max_low_addr_bound = std::max(addr, cur_fb_color_addr);
    PAddr min_hi_addr_bound = std::min(addr + size, cur_fb_color_addr + cur_fb_color_size);

    if (max_low_addr_bound <= min_hi_addr_bound) {
        ReloadColorBuffer();
    }

    max_low_addr_bound = std::max(addr, cur_fb_depth_addr);
    min_hi_addr_bound = std::min(addr + size, cur_fb_depth_addr + cur_fb_depth_size);

    if (max_low_addr_bound <= min_hi_addr_bound) {
        ReloadDepthBuffer();
    }

    // Notify cache of flush in case the region touches a cached resource
    res_cache.NotifyFlush(addr, size);
}

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
        LOG_CRITICAL(Render_OpenGL, "Unknown framebuffer texture color format %x", format);
        UNIMPLEMENTED();
        break;
    }

    state.texture_units[0].enabled_2d = true;
    state.texture_units[0].texture_2d = texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, texture.width, texture.height, 0,
        texture.gl_format, texture.gl_type, nullptr);
}

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
        LOG_CRITICAL(Render_OpenGL, "Unknown framebuffer texture depth format %x", format);
        UNIMPLEMENTED();
        break;
    }

    state.texture_units[0].enabled_2d = true;
    state.texture_units[0].texture_2d = texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, texture.width, texture.height, 0,
        texture.gl_format, texture.gl_type, nullptr);
}

void RasterizerOpenGL::SyncFramebuffer() {
    PAddr cur_fb_color_addr = Pica::registers.framebuffer.GetColorBufferPhysicalAddress();
    u32 new_fb_color_format = Pica::registers.framebuffer.color_format;

    PAddr cur_fb_depth_addr = Pica::registers.framebuffer.GetDepthBufferPhysicalAddress();
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
            LOG_CRITICAL(Render_OpenGL, "Unknown framebuffer depth format %x", new_fb_depth_format);
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

void RasterizerOpenGL::SyncDrawState() {
    // Sync the viewport
    GLsizei viewport_width = (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_x).ToFloat32() * 2;
    GLsizei viewport_height = (GLsizei)Pica::float24::FromRawFloat24(Pica::registers.viewport_size_y).ToFloat32() * 2;

    // OpenGL uses different y coordinates, so negate corner offset and flip origin
    // TODO: Ensure viewport_corner.x should not be negated or origin flipped
    glViewport((GLsizei)static_cast<float>(Pica::registers.viewport_corner.x),
                -(GLsizei)static_cast<float>(Pica::registers.viewport_corner.y)
                    + Pica::registers.framebuffer.GetHeight() - viewport_height,
                viewport_width, viewport_height);

    // Sync the cull mode
    switch (Pica::registers.cull_mode) {
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
        LOG_CRITICAL(Render_OpenGL, "Unknown cull mode %d", Pica::registers.cull_mode.Value());
        UNIMPLEMENTED();
        break;
    }

    // Sync depth test
    if (Pica::registers.output_merger.depth_test_enable) {
        state.depth.test_enabled = true;
        state.depth.test_func = PicaToGL::CompareFunc(Pica::registers.output_merger.depth_test_func);
    } else {
        state.depth.test_enabled = false;
    }

    // Sync depth writing
    if (Pica::registers.output_merger.depth_write_enable) {
        state.depth.write_mask = GL_TRUE;
    } else {
        state.depth.write_mask = GL_FALSE;
    }

    // TODO: Implement stencil test, mask, and op via: state.stencil.* = Pica::registers.output_merger.stencil_test.*

    // Sync blend state
    if (Pica::registers.output_merger.alphablend_enable) {
        state.blend.enabled = true;

        state.blend.color.red = (GLclampf)Pica::registers.output_merger.blend_const.r / 255.0f;
        state.blend.color.green = (GLclampf)Pica::registers.output_merger.blend_const.g / 255.0f;
        state.blend.color.blue = (GLclampf)Pica::registers.output_merger.blend_const.b / 255.0f;
        state.blend.color.alpha = (GLclampf)Pica::registers.output_merger.blend_const.a / 255.0f;

        state.blend.src_rgb_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_source_rgb);
        state.blend.dst_rgb_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_dest_rgb);
        state.blend.src_a_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_source_a);
        state.blend.dst_a_func = PicaToGL::BlendFunc(Pica::registers.output_merger.alpha_blending.factor_dest_a);
    } else {
        state.blend.enabled = false;
    }

    // Sync bound texture(s), upload if uncached
    const auto pica_textures = Pica::registers.GetTextures();

    for (int i = 0; i < 3; ++i) {
        const auto& texture = pica_textures[i];

        if (texture.enabled) {
            state.texture_units[i].enabled_2d = true;
            res_cache.LoadAndBindTexture(state, i, texture);
        } else {
            state.texture_units[i].enabled_2d = false;
        }
    }

    state.Apply();

    // Sync shader output register mapping to hw shader - 7 vectors with 4 components
    for (int i = 0; i < 7 * 4; ++i) {
        glUniform1i(uniform_out_maps + i, 0);
    }

    for (int i = 0; i < 7; ++i) {
        const auto& output_register_map = Pica::registers.vs_output_attributes[i];

        u32 semantics[4] = {
            output_register_map.map_x, output_register_map.map_y,
            output_register_map.map_z, output_register_map.map_w
        };

        // TODO: Might only need to do this once per shader? Not sure when/if out maps are modified.
        for (int comp = 0; comp < 4; ++comp) {
            if (semantics[comp] != Pica::Regs::VSOutputAttributes::INVALID) {
                glUniform1i(uniform_out_maps + semantics[comp], 4 * i + comp);
            }
        }
    }

    // Sync intial combiner buffer color
    GLfloat initial_combiner_color[4] = { Pica::registers.tev_combiner_buffer_color.r / 255.0f,
                                          Pica::registers.tev_combiner_buffer_color.g / 255.0f,
                                          Pica::registers.tev_combiner_buffer_color.b / 255.0f,
                                          Pica::registers.tev_combiner_buffer_color.a / 255.0f };

    glUniform4fv(uniform_tev_combiner_buffer_color, 1, initial_combiner_color);

    // Sync texture environment configurations to hw shader
    const auto tev_stages = Pica::registers.GetTevStages();
    for (unsigned tev_stage_idx = 0; tev_stage_idx < tev_stages.size(); ++tev_stage_idx) {
        const auto& stage = tev_stages[tev_stage_idx];
        const auto& uniform_tev_cfg = uniform_tev_cfgs[tev_stage_idx];

        GLint color_srcs[3] = { (GLint)stage.color_source1.Value(), (GLint)stage.color_source2.Value(), (GLint)stage.color_source3.Value() };
        GLint alpha_srcs[3] = { (GLint)stage.alpha_source1.Value(), (GLint)stage.alpha_source2.Value(), (GLint)stage.alpha_source3.Value() };
        GLint color_mods[3] = { (GLint)stage.color_modifier1.Value(), (GLint)stage.color_modifier2.Value(), (GLint)stage.color_modifier3.Value() };
        GLint alpha_mods[3] = { (GLint)stage.alpha_modifier1.Value(), (GLint)stage.alpha_modifier2.Value(), (GLint)stage.alpha_modifier3.Value() };
        GLfloat const_color[4] = { stage.const_r / 255.0f,
                                   stage.const_g / 255.0f,
                                   stage.const_b / 255.0f,
                                   stage.const_a / 255.0f };

        glUniform3iv(uniform_tev_cfg.color_sources, 1, color_srcs);
        glUniform3iv(uniform_tev_cfg.alpha_sources, 1, alpha_srcs);
        glUniform3iv(uniform_tev_cfg.color_modifiers, 1, color_mods);
        glUniform3iv(uniform_tev_cfg.alpha_modifiers, 1, alpha_mods);
        glUniform2i(uniform_tev_cfg.color_alpha_op, (GLint)stage.color_op.Value(), (GLint)stage.alpha_op.Value());
        glUniform2i(uniform_tev_cfg.color_alpha_multiplier, stage.GetColorMultiplier(), stage.GetAlphaMultiplier());
        glUniform4fv(uniform_tev_cfg.const_color, 1, const_color);
        glUniform2i(uniform_tev_cfg.updates_combiner_buffer_color_alpha,
                    Pica::registers.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferColor(tev_stage_idx),
                    Pica::registers.tev_combiner_buffer_input.TevStageUpdatesCombinerBufferAlpha(tev_stage_idx));
    }

    // Sync alpha testing to hw shader
    if (Pica::registers.output_merger.alpha_test.enable) {
        glUniform1i(uniform_alphatest_func, Pica::registers.output_merger.alpha_test.func);
        glUniform1f(uniform_alphatest_ref, Pica::registers.output_merger.alpha_test.ref / 255.0f);
    } else {
        glUniform1i(uniform_alphatest_func, Pica::registers.output_merger.Always);
    }
}

void RasterizerOpenGL::ReloadColorBuffer() {
    u8* color_buffer = Memory::GetPhysicalPointer(last_fb_color_addr);

    if (color_buffer == nullptr)
        return;

    u32 bytes_per_pixel = ColorFormatBytesPerPixel(fb_color_texture.format);

    std::unique_ptr<u8[]> temp_fb_color_buffer(new u8[fb_color_texture.width * fb_color_texture.height * bytes_per_pixel]);

    // Directly copy pixels. Internal OpenGL color formats are consistent so no conversion is necessary.
    // TODO: Evaluate whether u16/memcpy/u32 is faster for 2/3/4 bpp versus memcpy for all
    for (int y = 0; y < fb_color_texture.height; ++y) {
        for (int x = 0; x < fb_color_texture.width; ++x) {
            const u32 coarse_y = y & ~7;
            u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_color_texture.width * bytes_per_pixel;
            u32 gl_px_idx = x * bytes_per_pixel + y * fb_color_texture.width * bytes_per_pixel;

            u8* pixel = color_buffer + dst_offset;
            memcpy(&temp_fb_color_buffer[gl_px_idx], pixel, bytes_per_pixel);
        }
    }

    state.texture_units[0].enabled_2d = true;
    state.texture_units[0].texture_2d = fb_color_texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_color_texture.width, fb_color_texture.height,
                    fb_color_texture.gl_format, fb_color_texture.gl_type, temp_fb_color_buffer.get());
}

void RasterizerOpenGL::ReloadDepthBuffer() {
    // TODO: Appears to work, but double-check endianness of depth values and order of depth-stencil
    u8* depth_buffer = Memory::GetPhysicalPointer(last_fb_depth_addr);

    if (depth_buffer == nullptr) {
        return;
    }

    u32 bytes_per_pixel = Pica::Regs::BytesPerDepthPixel(fb_depth_texture.format);

    // OpenGL needs 4 bpp alignment for D24
    u32 gl_bpp = bytes_per_pixel == 3 ? 4 : bytes_per_pixel;

    std::unique_ptr<u8[]> temp_fb_depth_buffer(new u8[fb_depth_texture.width * fb_depth_texture.height * gl_bpp]);

    for (int y = 0; y < fb_depth_texture.height; ++y) {
        for (int x = 0; x < fb_depth_texture.width; ++x) {
            const u32 coarse_y = y & ~7;
            u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_depth_texture.width * bytes_per_pixel;
            u32 gl_px_idx = x + y * fb_depth_texture.width;

            switch (fb_depth_texture.format) {
            case Pica::Regs::DepthFormat::D16:
                ((u16*)temp_fb_depth_buffer.get())[gl_px_idx] = Color::DecodeD16(depth_buffer + dst_offset);
                break;
            case Pica::Regs::DepthFormat::D24:
                ((u32*)temp_fb_depth_buffer.get())[gl_px_idx] = Color::DecodeD24(depth_buffer + dst_offset);
                break;
            case Pica::Regs::DepthFormat::D24S8:
            {
                Math::Vec2<u32> depth_stencil = Color::DecodeD24S8(depth_buffer + dst_offset);
                ((u32*)temp_fb_depth_buffer.get())[gl_px_idx] = (depth_stencil.x << 8) | depth_stencil.y;
                break;
            }
            default:
                LOG_CRITICAL(Render_OpenGL, "Unknown memory framebuffer depth format %x", fb_depth_texture.format);
                UNIMPLEMENTED();
                break;
            }
        }
    }

    state.texture_units[0].enabled_2d = true;
    state.texture_units[0].texture_2d = fb_depth_texture.texture.GetHandle();
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb_depth_texture.width, fb_depth_texture.height,
                    fb_depth_texture.gl_format, fb_depth_texture.gl_type, temp_fb_depth_buffer.get());
}

void RasterizerOpenGL::CommitFramebuffer() {
    if (last_fb_color_addr != 0) {
        u8* color_buffer = Memory::GetPhysicalPointer(last_fb_color_addr);

        if (color_buffer != nullptr) {
            u32 bytes_per_pixel = ColorFormatBytesPerPixel(fb_color_texture.format);

            std::unique_ptr<u8[]> temp_gl_color_buffer(new u8[fb_color_texture.width * fb_color_texture.height * bytes_per_pixel]);

            state.texture_units[0].enabled_2d = true;
            state.texture_units[0].texture_2d = fb_color_texture.texture.GetHandle();
            state.Apply();

            glActiveTexture(GL_TEXTURE0);
            glGetTexImage(GL_TEXTURE_2D, 0, fb_color_texture.gl_format, fb_color_texture.gl_type, temp_gl_color_buffer.get());

            // Directly copy pixels. Internal OpenGL color formats are consistent so no conversion is necessary.
            // TODO: Evaluate whether u16/memcpy/u32 is faster for 2/3/4 bpp versus memcpy for all
            for (int y = 0; y < fb_color_texture.height; ++y) {
                for (int x = 0; x < fb_color_texture.width; ++x) {
                    const u32 coarse_y = y & ~7;
                    u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_color_texture.width * bytes_per_pixel;
                    u32 gl_px_idx = x * bytes_per_pixel + y * fb_color_texture.width * bytes_per_pixel;

                    u8* pixel = color_buffer + dst_offset;
                    memcpy(pixel, &temp_gl_color_buffer[gl_px_idx], bytes_per_pixel);
                }
            }
        }
    }

    if (last_fb_depth_addr != 0) {
        // TODO: Output seems correct visually, but doesn't quite match sw renderer output. One of them is wrong.
        u8* depth_buffer = Memory::GetPhysicalPointer(last_fb_depth_addr);

        if (depth_buffer != nullptr) {
            u32 bytes_per_pixel = Pica::Regs::BytesPerDepthPixel(fb_depth_texture.format);

            // OpenGL needs 4 bpp alignment for D24
            u32 gl_bpp = bytes_per_pixel == 3 ? 4 : bytes_per_pixel;

            std::unique_ptr<u8[]> temp_gl_depth_buffer(new u8[fb_depth_texture.width * fb_depth_texture.height * gl_bpp]);

            state.texture_units[0].enabled_2d = true;
            state.texture_units[0].texture_2d = fb_depth_texture.texture.GetHandle();
            state.Apply();

            glActiveTexture(GL_TEXTURE0);
            glGetTexImage(GL_TEXTURE_2D, 0, fb_depth_texture.gl_format, fb_depth_texture.gl_type, temp_gl_depth_buffer.get());

            for (int y = 0; y < fb_depth_texture.height; ++y) {
                for (int x = 0; x < fb_depth_texture.width; ++x) {
                    const u32 coarse_y = y & ~7;
                    u32 dst_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * fb_depth_texture.width * bytes_per_pixel;
                    u32 gl_px_idx = x + y * fb_depth_texture.width;

                    switch (fb_depth_texture.format) {
                    case Pica::Regs::DepthFormat::D16:
                        Color::EncodeD16(((u16*)temp_gl_depth_buffer.get())[gl_px_idx], depth_buffer + dst_offset);
                        break;
                    case Pica::Regs::DepthFormat::D24:
                        Color::EncodeD24(((u32*)temp_gl_depth_buffer.get())[gl_px_idx], depth_buffer + dst_offset);
                        break;
                    case Pica::Regs::DepthFormat::D24S8:
                    {
                        u32 depth_stencil = ((u32*)temp_gl_depth_buffer.get())[gl_px_idx];
                        Color::EncodeD24S8((depth_stencil >> 8), depth_stencil & 0xFF, depth_buffer + dst_offset);
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
