// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/pica.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"

OpenGLState OpenGLState::cur_state;
OpenGLState::ResourceHandles OpenGLState::cur_res_handles;

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

    memset(&cur_res_handles, 0, sizeof(cur_res_handles));
}

void OpenGLState::Apply() const {
    // Culling
    if (cull.enabled != cur_state.cull.enabled) {
        if (cull.enabled) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
    }

    if (cull.mode != cur_state.cull.mode) {
        glCullFace(cull.mode);
    }

    if (cull.front_face != cur_state.cull.front_face) {
        glFrontFace(cull.front_face);
    }

    // Depth test
    if (depth.test_enabled != cur_state.depth.test_enabled) {
        if (depth.test_enabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
    }

    if (depth.test_func != cur_state.depth.test_func) {
        glDepthFunc(depth.test_func);
    }

    // Depth mask
    if (depth.write_mask != cur_state.depth.write_mask) {
        glDepthMask(depth.write_mask);
    }

    // Color mask
    if (color_mask.red_enabled != cur_state.color_mask.red_enabled ||
            color_mask.green_enabled != cur_state.color_mask.green_enabled ||
            color_mask.blue_enabled != cur_state.color_mask.blue_enabled ||
            color_mask.alpha_enabled != cur_state.color_mask.alpha_enabled) {
        glColorMask(color_mask.red_enabled, color_mask.green_enabled,
                    color_mask.blue_enabled, color_mask.alpha_enabled);
    }

    // Stencil test
    if (stencil.test_enabled != cur_state.stencil.test_enabled) {
        if (stencil.test_enabled) {
            glEnable(GL_STENCIL_TEST);
        } else {
            glDisable(GL_STENCIL_TEST);
        }
    }

    if (stencil.test_func != cur_state.stencil.test_func ||
            stencil.test_ref != cur_state.stencil.test_ref ||
            stencil.test_mask != cur_state.stencil.test_mask) {
        glStencilFunc(stencil.test_func, stencil.test_ref, stencil.test_mask);
    }

    if (stencil.action_depth_fail != cur_state.stencil.action_depth_fail ||
            stencil.action_depth_pass != cur_state.stencil.action_depth_pass ||
            stencil.action_stencil_fail != cur_state.stencil.action_stencil_fail) {
        glStencilOp(stencil.action_stencil_fail, stencil.action_depth_fail, stencil.action_depth_pass);
    }

    // Stencil mask
    if (stencil.write_mask != cur_state.stencil.write_mask) {
        glStencilMask(stencil.write_mask);
    }

    // Blending
    if (blend.enabled != cur_state.blend.enabled) {
        if (blend.enabled) {
            glEnable(GL_BLEND);

            cur_state.logic_op = GL_COPY;
            glLogicOp(cur_state.logic_op);
            glDisable(GL_COLOR_LOGIC_OP);
        } else {
            glDisable(GL_BLEND);
            glEnable(GL_COLOR_LOGIC_OP);
        }
    }

    if (blend.color.red != cur_state.blend.color.red ||
            blend.color.green != cur_state.blend.color.green ||
            blend.color.blue != cur_state.blend.color.blue ||
            blend.color.alpha != cur_state.blend.color.alpha) {
        glBlendColor(blend.color.red, blend.color.green,
                     blend.color.blue, blend.color.alpha);
    }

    if (blend.src_rgb_func != cur_state.blend.src_rgb_func ||
            blend.dst_rgb_func != cur_state.blend.dst_rgb_func ||
            blend.src_a_func != cur_state.blend.src_a_func ||
            blend.dst_a_func != cur_state.blend.dst_a_func) {
        glBlendFuncSeparate(blend.src_rgb_func, blend.dst_rgb_func,
                            blend.src_a_func, blend.dst_a_func);
    }

    if (logic_op != cur_state.logic_op) {
        glLogicOp(logic_op);
    }

    // Textures
    for (unsigned i = 0; i < ARRAY_SIZE(texture_units); ++i) {
        const auto& texture_2d = texture_units[i].texture_2d.lock();
        GLuint texture_2d_handle = texture_2d != nullptr ? texture_2d->handle : 0;
        if (texture_2d_handle != cur_res_handles.texture_units[i].texture_2d) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, texture_2d_handle);
            cur_res_handles.texture_units[i].texture_2d = texture_2d_handle;
        }

        const auto& sampler = texture_units[i].sampler.lock();
        GLuint sampler_handle = sampler != nullptr ? sampler->handle : 0;
        if (sampler_handle != cur_res_handles.texture_units[i].sampler) {
            glBindSampler(i, sampler_handle);
            cur_res_handles.texture_units[i].sampler = sampler_handle;
        }
    }

    // Lighting LUTs
    for (unsigned i = 0; i < ARRAY_SIZE(lighting_luts); ++i) {
        const auto& texture_1d = lighting_luts[i].texture_1d.lock();
        GLuint texture_1d_handle = texture_1d != nullptr ? texture_1d->handle : 0;
        if (texture_1d_handle != cur_res_handles.lighting_luts[i].texture_1d) {
            glActiveTexture(GL_TEXTURE3 + i);
            glBindTexture(GL_TEXTURE_1D, texture_1d_handle);
            cur_res_handles.lighting_luts[i].texture_1d = texture_1d_handle;
        }
    }

    // Framebuffer
    const auto& read_framebuffer = draw.read_framebuffer.lock();
    GLuint read_framebuffer_handle = read_framebuffer != nullptr ? read_framebuffer->handle : 0;
    if (read_framebuffer_handle != cur_res_handles.draw.read_framebuffer) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer_handle);
        cur_res_handles.draw.read_framebuffer = read_framebuffer_handle;
    }
    const auto& draw_framebuffer = draw.draw_framebuffer.lock();
    GLuint draw_framebuffer_handle = draw_framebuffer != nullptr ? draw_framebuffer->handle : 0;
    if (draw_framebuffer_handle != cur_res_handles.draw.draw_framebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_framebuffer_handle);
        cur_res_handles.draw.draw_framebuffer = draw_framebuffer_handle;
    }

    // Vertex array
    const auto& vertex_array = draw.vertex_array.lock();
    GLuint vertex_array_handle = vertex_array != nullptr ? vertex_array->handle : 0;
    if (vertex_array_handle != cur_res_handles.draw.vertex_array) {
        glBindVertexArray(vertex_array_handle);
        cur_res_handles.draw.vertex_array = vertex_array_handle;
    }

    // Vertex buffer
    const auto& vertex_buffer = draw.vertex_buffer.lock();
    GLuint vertex_buffer_handle = vertex_buffer != nullptr ? vertex_buffer->handle : 0;
    if (vertex_buffer_handle != cur_res_handles.draw.vertex_buffer) {
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_handle);
        cur_res_handles.draw.vertex_buffer = vertex_buffer_handle;
    }

    // Uniform buffer
    const auto& uniform_buffer = draw.uniform_buffer.lock();
    GLuint uniform_buffer_handle = uniform_buffer != nullptr ? uniform_buffer->handle : 0;
    if (uniform_buffer_handle != cur_res_handles.draw.uniform_buffer) {
        glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer_handle);
        cur_res_handles.draw.uniform_buffer = uniform_buffer_handle;
    }

    // Shader program
    const auto& shader_program = draw.shader_program.lock();
    GLuint shader_program_handle = shader_program != nullptr ? shader_program->handle : 0;
    if (shader_program_handle != cur_res_handles.draw.shader_program) {
        glUseProgram(shader_program_handle);
        cur_res_handles.draw.shader_program = shader_program_handle;
    }

    cur_state = *this;
}

GLenum OpenGLState::CheckFBStatus(GLenum target) {
    GLenum fb_status = glCheckFramebufferStatus(target);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
        const char* fb_description = (target == GL_READ_FRAMEBUFFER ? "READ" : (target == GL_DRAW_FRAMEBUFFER ? "DRAW" : "UNK"));
        LOG_CRITICAL(Render_OpenGL, "OpenGL %s framebuffer check failed, status %X", fb_description, fb_status);
    }

    return fb_status;
}

void OpenGLState::ResetTexture(const OGLTexture* texture) {
    for (auto& unit : cur_state.texture_units) {
        if (unit.texture_2d.lock().get() == texture) {
            unit.texture_2d.reset();
        }
    }

    cur_state.Apply();
}

void OpenGLState::ResetSampler(const OGLSampler* sampler) {
    for (auto& unit : cur_state.texture_units) {
        if (unit.sampler.lock().get() == sampler) {
            unit.sampler.reset();
        }
    }

    cur_state.Apply();
}

void OpenGLState::ResetProgram(const OGLShader* program) {
    if (cur_state.draw.shader_program.lock().get() == program) {
        cur_state.draw.shader_program.reset();
    }

    cur_state.Apply();
}

void OpenGLState::ResetBuffer(const OGLBuffer* buffer) {
    if (cur_state.draw.vertex_buffer.lock().get() == buffer) {
        cur_state.draw.vertex_buffer.reset();
    }
    if (cur_state.draw.uniform_buffer.lock().get() == buffer) {
        cur_state.draw.uniform_buffer.reset();
    }

    cur_state.Apply();
}

void OpenGLState::ResetVertexArray(const OGLVertexArray* vertex_array) {
    if (cur_state.draw.vertex_array.lock().get() == vertex_array) {
        cur_state.draw.vertex_array.reset();
    }

    cur_state.Apply();
}

void OpenGLState::ResetFramebuffer(const OGLFramebuffer* framebuffer) {
    if (cur_state.draw.read_framebuffer.lock().get() == framebuffer) {
        cur_state.draw.read_framebuffer.reset();
    }
    if (cur_state.draw.draw_framebuffer.lock().get() == framebuffer) {
        cur_state.draw.draw_framebuffer.reset();
    }

    cur_state.Apply();
}
