// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <set>
#include <tuple>

#include <glad/glad.h>

#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/logging/log.h"

#include "video_core/renderer_opengl/gl_state.h"

std::set<OpenGLState*> states;
OpenGLState default_state;
OpenGLState* cur_state = &default_state;

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
        texture_unit.texture_1d = 0;
        texture_unit.texture_2d = 0;
        texture_unit.sampler = 0;
    }

    active_texture_unit = GL_TEXTURE0;

    draw.read_framebuffer = 0;
    draw.draw_framebuffer = 0;
    draw.vertex_array = 0;
    draw.vertex_buffer = 0;
    draw.uniform_buffer = 0;
    draw.shader_program = 0;

    states.insert(this);
}

OpenGLState::~OpenGLState() {
    states.erase(this);
}

OpenGLState* OpenGLState::GetCurrentState() {
    return cur_state;
}

void OpenGLState::MakeCurrent() {
    if (cur_state == this) {
        return;
    }

    SetCullEnabled(GetCullEnabled());
    SetCullMode(GetCullMode());
    SetCullFrontFace(GetCullFrontFace());

    SetDepthTestEnabled(GetDepthTestEnabled());
    SetDepthFunc(GetDepthFunc());
    SetDepthWriteMask(GetDepthWriteMask());

    SetColorMask(GetColorMask());

    SetStencilTestEnabled(GetStencilTestEnabled());
    SetStencilFunc(GetStencilFunc());
    SetStencilOp(GetStencilOp());
    SetStencilWriteMask(GetStencilWriteMask());

    SetBlendEnabled(GetBlendEnabled());
    SetBlendFunc(GetBlendFunc());
    SetBlendColor(GetBlendColor());

    SetLogicOp(GetLogicOp());

    GLenum prev_active_texture_unit = active_texture_unit;
    for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        active_texture_unit = GL_TEXTURE0 + i;
        SetTexture1D(texture_units[i].texture_1d);
        SetTexture2D(texture_units[i].texture_2d);
        SetSampler(texture_units[i].sampler);
    }
    active_texture_unit = prev_active_texture_unit;
    glActiveTexture(cur_state->active_texture_unit);

    SetActiveTextureUnit(GetActiveTextureUnit());

    SetReadFramebuffer(GetReadFramebuffer());
    SetDrawFramebuffer(GetDrawFramebuffer());
    SetVertexArray(GetVertexArray());
    SetVertexBuffer(GetVertexBuffer());
    SetUniformBuffer(GetUniformBuffer());
    SetShaderProgram(GetShaderProgram());

    cur_state = this;
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

void OpenGLState::SetColorMask(std::tuple<GLboolean, GLboolean, GLboolean, GLboolean> n_rgba_enabled) {
    GLboolean n_red_enabled, n_green_enabled, n_blue_enabled, n_alpha_enabled;
    std::tie(n_red_enabled, n_green_enabled, n_blue_enabled, n_alpha_enabled) = n_rgba_enabled;

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

void OpenGLState::SetStencilFunc(std::tuple<GLenum, GLint, GLint> n_funcs) {
    GLenum n_test_func;
    GLint n_test_ref, n_test_mask;
    std::tie(n_test_func, n_test_ref, n_test_mask) = n_funcs;

    if (n_test_func != cur_state->stencil.test_func ||
            n_test_ref != cur_state->stencil.test_ref ||
            n_test_mask != cur_state->stencil.test_mask) {
        glStencilFunc(n_test_func, n_test_ref, n_test_mask);
    }
    stencil.test_func = n_test_func;
    stencil.test_ref = n_test_ref;
    stencil.test_mask = n_test_mask;
}

void OpenGLState::SetStencilOp(std::tuple<GLenum, GLenum, GLenum> n_actions) {
    GLenum n_action_stencil_fail, n_action_depth_fail, n_action_depth_pass;
    std::tie(n_action_stencil_fail, n_action_depth_fail, n_action_depth_pass) = n_actions;

    if (n_action_stencil_fail != cur_state->stencil.action_stencil_fail ||
            n_action_depth_fail != cur_state->stencil.action_depth_fail ||
            n_action_depth_pass != cur_state->stencil.action_depth_pass) {
        glStencilOp(n_action_stencil_fail, n_action_depth_fail, n_action_depth_pass);
    }
    stencil.action_stencil_fail = n_action_stencil_fail;
    stencil.action_depth_fail = n_action_depth_fail;
    stencil.action_depth_pass = n_action_depth_pass;
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

void OpenGLState::SetBlendFunc(std::tuple<GLenum, GLenum, GLenum, GLenum> n_funcs) {
    GLenum n_src_rgb_func, n_dst_rgb_func, n_src_a_func, n_dst_a_func;
    std::tie(n_src_rgb_func, n_dst_rgb_func, n_src_a_func, n_dst_a_func) = n_funcs;

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

void OpenGLState::SetBlendColor(std::tuple<GLclampf, GLclampf, GLclampf, GLclampf> n_color) {
    GLclampf n_red, n_green, n_blue, n_alpha;
    std::tie(n_red, n_green, n_blue, n_alpha) = n_color;

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

void OpenGLState::SetTexture1D(GLuint n_texture_1d) {
    unsigned unit_index = active_texture_unit - GL_TEXTURE0;
    ASSERT(unit_index < ARRAY_SIZE(texture_units));

    if (n_texture_1d != cur_state->texture_units[unit_index].texture_1d) {
        glBindTexture(GL_TEXTURE_1D, n_texture_1d);
    }
    texture_units[unit_index].texture_1d = n_texture_1d;
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

void OpenGLState::ResetTexture(GLuint handle) {
    for (OpenGLState* state : states) {
        for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
            if (state->texture_units[i].texture_1d == handle) {
                if (state == cur_state) {
                    GLenum prev_active_texture_unit = state->active_texture_unit;
                    state->SetActiveTextureUnit(GL_TEXTURE0 + i);
                    state->SetTexture1D(0);
                    state->SetActiveTextureUnit(prev_active_texture_unit);
                } else {
                    state->texture_units[i].texture_1d = 0;
                }
            }

            if (state->texture_units[i].texture_2d == handle) {
                if (state == cur_state) {
                    GLenum prev_active_texture_unit = state->active_texture_unit;
                    state->SetActiveTextureUnit(GL_TEXTURE0 + i);
                    state->SetTexture2D(0);
                    state->SetActiveTextureUnit(prev_active_texture_unit);
                } else {
                    state->texture_units[i].texture_2d = 0;
                }
            }
        }
    }
}

void OpenGLState::ResetSampler(GLuint handle) {
    for (OpenGLState* state : states) {
        for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
            if (state->texture_units[i].sampler == handle) {
                if (state == cur_state) {
                    GLenum prev_active_texture_unit = state->active_texture_unit;
                    state->SetActiveTextureUnit(GL_TEXTURE0 + i);
                    state->SetSampler(0);
                    state->SetActiveTextureUnit(prev_active_texture_unit);
                } else {
                    state->texture_units[i].sampler = 0;
                }
            }
        }
    }
}

void OpenGLState::ResetProgram(GLuint handle) {
    for (OpenGLState* state : states) {
        if (state->draw.shader_program == handle) {
            if (state == cur_state) {
                state->SetShaderProgram(0);
            } else {
                state->draw.shader_program = 0;
            }
        }
    }
}

void OpenGLState::ResetBuffer(GLuint handle) {
    for (OpenGLState* state : states) {
        if (state->draw.vertex_buffer == handle) {
            if (state == cur_state) {
                state->SetVertexBuffer(0);
            } else {
                state->draw.vertex_buffer = 0;
            }
        }

        if (state->draw.uniform_buffer == handle) {
            if (state == cur_state) {
                state->SetUniformBuffer(0);
            } else {
                state->draw.uniform_buffer = 0;
            }
        }
    }
}

void OpenGLState::ResetVertexArray(GLuint handle) {
    for (OpenGLState* state : states) {
        if (state->draw.vertex_array == handle) {
            if (state == cur_state) {
                state->SetVertexArray(0);
            } else {
                state->draw.vertex_array = 0;
            }
        }
    }
}

void OpenGLState::ResetFramebuffer(GLuint handle) {
    for (OpenGLState* state : states) {
        if (state->draw.read_framebuffer == handle) {
            if (state == cur_state) {
                state->SetReadFramebuffer(0);
            } else {
                state->draw.read_framebuffer = 0;
            }
        }

        if (state->draw.draw_framebuffer == handle) {
            if (state == cur_state) {
                state->SetDrawFramebuffer(0);
            } else {
                state->draw.draw_framebuffer = 0;
            }
        }
    }
}

GLenum OpenGLState::CheckBoundFBStatus(GLenum target) {
    GLenum fb_status = glCheckFramebufferStatus(target);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
        const char* fb_description = (target == GL_READ_FRAMEBUFFER ? "READ" : (target == GL_DRAW_FRAMEBUFFER ? "DRAW" : "UNK"));
        LOG_CRITICAL(Render_OpenGL, "OpenGL %s framebuffer check failed, status %X", fb_description, fb_status);
    }

    return fb_status;
}

OpenGLState OpenGLState::ApplyTransferState(GLuint src_tex, GLuint read_framebuffer, GLuint dst_tex, GLuint draw_framebuffer) {
    OpenGLState old_state = *cur_state;

    cur_state->SetColorMask(std::make_tuple<>(true, true, true, true));
    cur_state->SetDepthWriteMask(true);
    cur_state->SetStencilWriteMask(true);
    if (src_tex != 0) {
        cur_state->ResetTexture(src_tex);
    }
    cur_state->SetReadFramebuffer(read_framebuffer);
    if (dst_tex != 0) {
        cur_state->ResetTexture(dst_tex);
    }
    cur_state->SetDrawFramebuffer(draw_framebuffer);

    return old_state;
}

void OpenGLState::UndoTransferState(OpenGLState &old_state) {
    cur_state->SetColorMask(old_state.GetColorMask());
    cur_state->SetDepthWriteMask(old_state.GetDepthWriteMask());
    cur_state->SetStencilWriteMask(old_state.GetStencilWriteMask());
    cur_state->SetReadFramebuffer(old_state.GetReadFramebuffer());
    cur_state->SetDrawFramebuffer(old_state.GetDrawFramebuffer());

    // NOTE: Textures that were reset in ApplyTransferState() are NOT restored
}
