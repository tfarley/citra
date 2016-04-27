// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <glad/glad.h>

#include "common/common_funcs.h"
#include "common/logging/log.h"

#include "video_core/renderer_opengl/gl_state.h"

static OpenGLState default_state;
OpenGLState* OpenGLState::cur_state = &default_state;

OpenGLState::OpenGLState() {
    // These all match default OpenGL values
    cull.enabled = false;
    cull.mode = GL_BACK;
    cull.front_face = GL_CCW;

    depth.test_enabled = false;
    depth.test_func = GL_LESS;
    depth.write_mask = GL_TRUE;

    color_mask.red_enabled = GL_TRUE;
    color_mask.green_enabled = GL_TRUE;
    color_mask.blue_enabled = GL_TRUE;
    color_mask.alpha_enabled = GL_TRUE;

    stencil.test_enabled = false;
    stencil.test_func = GL_ALWAYS;
    stencil.test_ref = 0;
    stencil.test_mask = -1;
    stencil.write_mask = -1;
    stencil.action_depth_fail = GL_KEEP;
    stencil.action_depth_pass = GL_KEEP;
    stencil.action_stencil_fail = GL_KEEP;

    blend.enabled = false;
    blend.src_rgb_func = GL_ONE;
    blend.dst_rgb_func = GL_ZERO;
    blend.src_a_func = GL_ONE;
    blend.dst_a_func = GL_ZERO;
    blend.color.red = 0.0f;
    blend.color.green = 0.0f;
    blend.color.blue = 0.0f;
    blend.color.alpha = 0.0f;

    logic_op = GL_COPY;

    for (auto& texture_unit : texture_units) {
        texture_unit.texture_2d = 0;
        texture_unit.sampler = 0;
    }

    for (auto& lut : lighting_luts) {
        lut.texture_1d = 0;
    }

    active_texture_unit = GL_TEXTURE0;

    draw.read_framebuffer = 0;
    draw.draw_framebuffer = 0;
    draw.vertex_array = 0;
    draw.vertex_buffer = 0;
    draw.uniform_buffer = 0;
    draw.shader_program = 0;
}

OpenGLState* OpenGLState::GetCurrentState() {
    return cur_state;
}

void OpenGLState::SetCullEnabled(bool n_enabled) {
    if (n_enabled != cur_state->cull.enabled) {
        if (n_enabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }
    cull.enabled = n_enabled;
}

void OpenGLState::SetCullMode(GLenum n_mode) {
    if (n_mode != cur_state->cull.mode) {
        glCullFace(n_mode);
    }
    cull.mode = n_mode;
}

void OpenGLState::SetCullFrontFace(GLenum n_front_face) {
    if (n_front_face != cur_state->cull.front_face) {
        glFrontFace(n_front_face);
    }
    cull.front_face = n_front_face;
}

void OpenGLState::SetDepthTestEnabled(bool n_test_enabled) {
    if (n_test_enabled != cur_state->depth.test_enabled) {
        if (n_test_enabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }
    depth.test_enabled = n_test_enabled;
}

void OpenGLState::SetDepthFunc(GLenum n_test_func) {
    if (n_test_func != cur_state->depth.test_func) {
        glDepthFunc(n_test_func);
    }
    depth.test_func = n_test_func;
}

void OpenGLState::SetDepthWriteMask(GLboolean n_write_mask) {
    if (n_write_mask != cur_state->depth.write_mask) {
        glDepthMask(n_write_mask);
    }
    depth.write_mask = n_write_mask;
}

void OpenGLState::SetColorMask(GLboolean n_red_enabled, GLboolean n_green_enabled, GLboolean n_blue_enabled, GLboolean n_alpha_enabled) {
    if (n_red_enabled != cur_state->color_mask.red_enabled ||
            n_green_enabled != cur_state->color_mask.green_enabled ||
            n_blue_enabled != cur_state->color_mask.blue_enabled ||
            n_alpha_enabled != cur_state->color_mask.alpha_enabled) {
        glColorMask(n_red_enabled, n_green_enabled,
                    n_blue_enabled, n_alpha_enabled);
    }
    color_mask.red_enabled = n_red_enabled;
    color_mask.green_enabled = n_green_enabled;
    color_mask.blue_enabled = n_blue_enabled;
    color_mask.alpha_enabled = n_alpha_enabled;
}

void OpenGLState::SetStencilTestEnabled(bool n_test_enabled) {
    if (n_test_enabled != cur_state->stencil.test_enabled) {
        if (n_test_enabled) {
            glEnable(GL_STENCIL_TEST);
        } else {
            glDisable(GL_STENCIL_TEST);
        }
    }
    stencil.test_enabled = n_test_enabled;
}

void OpenGLState::SetStencilFunc(GLenum n_test_func, GLint n_test_ref, GLuint n_test_mask) {
    if (n_test_func != cur_state->stencil.test_func ||
            n_test_ref != cur_state->stencil.test_ref ||
            n_test_mask != cur_state->stencil.test_mask) {
        glStencilFunc(n_test_func, n_test_ref, n_test_mask);
    }
    stencil.test_func = n_test_func;
    stencil.test_ref = n_test_ref;
    stencil.test_mask = n_test_mask;
}

void OpenGLState::SetStencilOp(GLenum n_action_stencil_fail, GLenum n_action_depth_fail, GLenum n_action_depth_pass) {
    if (n_action_stencil_fail != cur_state->stencil.action_depth_fail ||
            n_action_depth_fail != cur_state->stencil.action_depth_pass ||
            n_action_depth_pass != cur_state->stencil.action_stencil_fail) {
        glStencilOp(n_action_stencil_fail, n_action_depth_fail, n_action_depth_pass);
    }
    stencil.action_depth_fail = n_action_stencil_fail;
    stencil.action_depth_pass = n_action_depth_fail;
    stencil.action_stencil_fail = n_action_depth_pass;
}

void OpenGLState::SetStencilWriteMask(GLuint n_write_mask) {
    if (n_write_mask != cur_state->stencil.write_mask) {
        glStencilMask(n_write_mask);
    }
    stencil.write_mask = n_write_mask;
}

void OpenGLState::SetBlendEnabled(bool n_enabled) {
    if (n_enabled != cur_state->blend.enabled) {
        if (n_enabled) {
            glEnable(GL_BLEND);

            SetLogicOp(GL_COPY);
            glDisable(GL_COLOR_LOGIC_OP);
        } else {
            glDisable(GL_BLEND);
            glEnable(GL_COLOR_LOGIC_OP);
        }
    }
    blend.enabled = n_enabled;
}

void OpenGLState::SetBlendFunc(GLenum n_src_rgb_func, GLenum n_dst_rgb_func, GLenum n_src_a_func, GLenum n_dst_a_func) {
    if (n_src_rgb_func != cur_state->blend.src_rgb_func ||
            n_dst_rgb_func != cur_state->blend.dst_rgb_func ||
            n_src_a_func != cur_state->blend.src_a_func ||
            n_dst_a_func != cur_state->blend.dst_a_func) {
        glBlendFuncSeparate(n_src_rgb_func, n_dst_rgb_func,
                            n_src_a_func, n_dst_a_func);
    }
    blend.src_rgb_func = n_src_rgb_func;
    blend.dst_rgb_func = n_dst_rgb_func;
    blend.src_a_func = n_src_a_func;
    blend.dst_a_func = n_dst_a_func;
}

void OpenGLState::SetBlendColor(GLclampf n_red, GLclampf n_green, GLclampf n_blue, GLclampf n_alpha) {
    if (n_red != cur_state->blend.color.red ||
            n_green != cur_state->blend.color.green ||
            n_blue != cur_state->blend.color.blue ||
            n_alpha != cur_state->blend.color.alpha) {
        glBlendColor(n_red, n_green,
                     n_blue, n_alpha);
    }
    blend.color.red = n_red;
    blend.color.green = n_green;
    blend.color.blue = n_blue;
    blend.color.alpha = n_alpha;
}

void OpenGLState::SetLogicOp(GLenum n_logic_op) {
    if (n_logic_op != cur_state->logic_op) {
        glLogicOp(n_logic_op);
    }
    logic_op = n_logic_op;
}

void OpenGLState::SetTexture2D(GLuint n_texture_2d) {
    unsigned unit_index = active_texture_unit - GL_TEXTURE0;
    ASSERT(unit_index < ARRAY_SIZE(texture_units));

    if (n_texture_2d != cur_state->texture_units[unit_index].texture_2d) {
        glBindTexture(GL_TEXTURE_2D, n_texture_2d);
    }
    texture_units[unit_index].texture_2d = n_texture_2d;
}

void OpenGLState::SetSampler(GLuint n_sampler) {
    unsigned unit_index = active_texture_unit - GL_TEXTURE0;
    ASSERT(unit_index < ARRAY_SIZE(texture_units));

    if (n_sampler != cur_state->texture_units[unit_index].sampler) {
        glBindSampler(unit_index, n_sampler);
    }
    texture_units[unit_index].sampler = n_sampler;
}

void OpenGLState::SetLUTTexture1D(GLuint n_texture_1d) {
    unsigned unit_index = active_texture_unit - GL_TEXTURE3;
    ASSERT(unit_index < ARRAY_SIZE(lighting_luts));

    if (n_texture_1d != cur_state->lighting_luts[unit_index].texture_1d) {
        glBindTexture(GL_TEXTURE_1D, n_texture_1d);
    }
    lighting_luts[unit_index].texture_1d = n_texture_1d;
}

void OpenGLState::SetActiveTextureUnit(GLenum n_active_texture_unit) {
    if (n_active_texture_unit != cur_state->active_texture_unit) {
        glActiveTexture(n_active_texture_unit);
    }
    active_texture_unit = n_active_texture_unit;
}

void OpenGLState::SetReadFramebuffer(GLuint n_read_framebuffer) {
    if (n_read_framebuffer != cur_state->draw.read_framebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, n_read_framebuffer);
    }
    draw.read_framebuffer = n_read_framebuffer;
}

void OpenGLState::SetDrawFramebuffer(GLuint n_draw_framebuffer) {
    if (n_draw_framebuffer != cur_state->draw.draw_framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, n_draw_framebuffer);
    }
    draw.draw_framebuffer = n_draw_framebuffer;
}

void OpenGLState::SetVertexArray(GLuint n_vertex_array) {
    if (n_vertex_array != cur_state->draw.vertex_array) {
        glBindVertexArray(n_vertex_array);
    }
    draw.vertex_array = n_vertex_array;
}

void OpenGLState::SetVertexBuffer(GLuint n_vertex_buffer) {
    if (n_vertex_buffer != cur_state->draw.vertex_buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, n_vertex_buffer);
    }
    draw.vertex_buffer = n_vertex_buffer;
}

void OpenGLState::SetUniformBuffer(GLuint n_uniform_buffer) {
    if (n_uniform_buffer != cur_state->draw.uniform_buffer) {
        glBindBuffer(GL_UNIFORM_BUFFER, n_uniform_buffer);
    }
    draw.uniform_buffer = n_uniform_buffer;
}

void OpenGLState::SetShaderProgram(GLuint n_shader_program) {
    if (n_shader_program != cur_state->draw.shader_program) {
        glUseProgram(n_shader_program);
    }
    draw.shader_program = n_shader_program;
}

void OpenGLState::MakeCurrent() {
    if (cur_state == this) {
        return;
    }

    SetCullEnabled(cull.enabled);
    SetCullMode(cull.mode);
    SetCullFrontFace(cull.front_face);

    SetDepthTestEnabled(depth.test_enabled);
    SetDepthFunc(depth.test_func);
    SetDepthWriteMask(depth.write_mask);

    SetColorMask(color_mask.red_enabled, color_mask.green_enabled, color_mask.blue_enabled, color_mask.alpha_enabled);

    SetStencilTestEnabled(stencil.test_enabled);
    SetStencilFunc(stencil.test_func, stencil.test_ref, stencil.test_mask);
    SetStencilOp(stencil.action_stencil_fail, stencil.action_depth_fail, stencil.action_depth_pass);
    SetStencilWriteMask(stencil.write_mask);

    SetBlendEnabled(blend.enabled);
    SetBlendFunc(blend.src_rgb_func, blend.dst_rgb_func, blend.src_a_func, blend.dst_a_func);
    SetBlendColor(blend.color.red, blend.color.green, blend.color.blue, blend.color.alpha);

    SetLogicOp(logic_op);

    for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
        SetActiveTextureUnit(GL_TEXTURE0 + i);
        SetTexture2D(texture_units[i].texture_2d);
        SetSampler(texture_units[i].sampler);
    }

    for (unsigned i = 0; i < ARRAY_SIZE(lighting_luts); ++i) {
        SetActiveTextureUnit(GL_TEXTURE3 + i);
        SetLUTTexture1D(lighting_luts[i].texture_1d);
    }

    SetActiveTextureUnit(active_texture_unit);

    SetReadFramebuffer(draw.read_framebuffer);
    SetDrawFramebuffer(draw.draw_framebuffer);
    SetVertexArray(draw.vertex_array);
    SetVertexBuffer(draw.vertex_buffer);
    SetUniformBuffer(draw.uniform_buffer);
    SetShaderProgram(draw.shader_program);

    cur_state = this;
}

GLenum OpenGLState::CheckFBStatus(GLenum target) {
    GLenum fb_status = glCheckFramebufferStatus(target);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
        const char* fb_description = (target == GL_READ_FRAMEBUFFER ? "READ" : (target == GL_DRAW_FRAMEBUFFER ? "DRAW" : "UNK"));
        LOG_CRITICAL(Render_OpenGL, "OpenGL %s framebuffer check failed, status %X", fb_description, fb_status);
    }

    return fb_status;
}

void OpenGLState::ResetTexture(GLuint handle) {
    for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
        if (cur_state->texture_units[i].texture_2d == handle) {
            cur_state->SetActiveTextureUnit(GL_TEXTURE0 + i);
            cur_state->SetTexture2D(0);
        }
    }
    for (unsigned i = 0; i < ARRAY_SIZE(lighting_luts); ++i) {
        if (cur_state->lighting_luts[i].texture_1d == handle) {
            cur_state->SetActiveTextureUnit(GL_TEXTURE3 + i);
            cur_state->SetLUTTexture1D(0);
        }
    }
}

void OpenGLState::ResetSampler(GLuint handle) {
    for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
        if (cur_state->texture_units[i].texture_2d == handle) {
            cur_state->SetActiveTextureUnit(GL_TEXTURE0 + i);
            cur_state->SetSampler(0);
        }
    }
}

void OpenGLState::ResetProgram(GLuint handle) {
    if (cur_state->draw.shader_program == handle) {
        cur_state->SetShaderProgram(0);
    }
}

void OpenGLState::ResetBuffer(GLuint handle) {
    if (cur_state->draw.vertex_buffer == handle) {
        cur_state->SetVertexBuffer(0);
    }
    if (cur_state->draw.uniform_buffer == handle) {
        cur_state->SetUniformBuffer(0);
    }
}

void OpenGLState::ResetVertexArray(GLuint handle) {
    if (cur_state->draw.vertex_array == handle) {
        cur_state->SetVertexArray(0);
    }
}

void OpenGLState::ResetFramebuffer(GLuint handle) {
    if (cur_state->draw.read_framebuffer == handle) {
        cur_state->SetReadFramebuffer(0);
    }
    if (cur_state->draw.draw_framebuffer == handle) {
        cur_state->SetDrawFramebuffer(0);
    }
}
