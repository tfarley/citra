// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <glad/glad.h>

class OpenGLState {
public:
    OpenGLState();
    ~OpenGLState();

    /// Get a pointer to the currently bound state tracker object
    static OpenGLState* GetCurrentState();

    /// Apply this state as the current OpenGL state
    void MakeCurrent();

    /// Setter functions for OpenGL state
    void SetCullEnabled(bool n_enabled);
    void SetCullMode(GLenum n_mode);
    void SetCullFrontFace(GLenum n_front_face);

    void SetDepthTestEnabled(bool n_test_enabled);
    void SetDepthFunc(GLenum n_test_func);
    void SetDepthWriteMask(GLboolean n_write_mask);

    void SetColorMask(GLboolean n_red_enabled, GLboolean n_green_enabled, GLboolean n_blue_enabled, GLboolean n_alpha_enabled);

    void SetStencilTestEnabled(bool n_test_enabled);
    void SetStencilFunc(GLenum n_test_func, GLint n_test_ref, GLuint n_test_mask);
    void SetStencilOp(GLenum n_action_stencil_fail, GLenum n_action_depth_fail, GLenum n_action_depth_pass);
    void SetStencilWriteMask(GLuint n_write_mask);

    void SetBlendEnabled(bool n_enabled);
    void SetBlendFunc(GLenum n_src_rgb_func, GLenum n_dst_rgb_func, GLenum n_src_a_func, GLenum n_dst_a_func);
    void SetBlendColor(GLclampf n_red, GLclampf n_green, GLclampf n_blue, GLclampf n_alpha);

    void SetLogicOp(GLenum n_logic_op);

    void SetTexture1D(GLuint n_texture_1d);
    void SetTexture2D(GLuint n_texture_2d);
    void SetSampler(GLuint n_sampler);

    void SetActiveTextureUnit(GLenum n_active_texture_unit);

    void SetReadFramebuffer(GLuint n_read_framebuffer);
    void SetDrawFramebuffer(GLuint n_draw_framebuffer);
    void SetVertexArray(GLuint n_vertex_array);
    void SetVertexBuffer(GLuint n_vertex_buffer);
    void SetUniformBuffer(GLuint n_uniform_buffer);
    void SetShaderProgram(GLuint n_shader_program);

    /// Resets and unbinds any references to the given resource across all existing states
    static void ResetTexture(GLuint handle);
    static void ResetSampler(GLuint handle);
    static void ResetProgram(GLuint handle);
    static void ResetBuffer(GLuint handle);
    static void ResetVertexArray(GLuint handle);
    static void ResetFramebuffer(GLuint handle);

    /// Check the status of the currently bound OpenGL read or draw framebuffer configuration
    static GLenum CheckBoundFBStatus(GLenum target);

private:
    struct {
        bool enabled; // GL_CULL_FACE
        GLenum mode; // GL_CULL_FACE_MODE
        GLenum front_face; // GL_FRONT_FACE
    } cull;

    struct {
        bool test_enabled; // GL_DEPTH_TEST
        GLenum test_func; // GL_DEPTH_FUNC
        GLboolean write_mask; // GL_DEPTH_WRITEMASK
    } depth;

    struct {
        GLboolean red_enabled;
        GLboolean green_enabled;
        GLboolean blue_enabled;
        GLboolean alpha_enabled;
    } color_mask; // GL_COLOR_WRITEMASK

    struct {
        bool test_enabled; // GL_STENCIL_TEST
        GLenum test_func; // GL_STENCIL_FUNC
        GLint test_ref; // GL_STENCIL_REF
        GLuint test_mask; // GL_STENCIL_VALUE_MASK
        GLenum action_stencil_fail; // GL_STENCIL_FAIL
        GLenum action_depth_fail; // GL_STENCIL_PASS_DEPTH_FAIL
        GLenum action_depth_pass; // GL_STENCIL_PASS_DEPTH_PASS
        GLuint write_mask; // GL_STENCIL_WRITEMASK
    } stencil;

    struct {
        bool enabled; // GL_BLEND
        GLenum src_rgb_func; // GL_BLEND_SRC_RGB
        GLenum dst_rgb_func; // GL_BLEND_DST_RGB
        GLenum src_a_func; // GL_BLEND_SRC_ALPHA
        GLenum dst_a_func; // GL_BLEND_DST_ALPHA

        struct {
            GLclampf red;
            GLclampf green;
            GLclampf blue;
            GLclampf alpha;
        } color; // GL_BLEND_COLOR
    } blend;

    GLenum logic_op; // GL_LOGIC_OP_MODE

    // 3 texture units - one for each that is used in PICA fragment shader emulation
    struct {
        GLuint texture_1d; // GL_TEXTURE_BINDING_1D
        GLuint texture_2d; // GL_TEXTURE_BINDING_2D
        GLuint sampler; // GL_SAMPLER_BINDING
    } texture_units[9];

    GLenum active_texture_unit; // GL_ACTIVE_TEXTURE

    struct {
        GLuint read_framebuffer; // GL_READ_FRAMEBUFFER_BINDING
        GLuint draw_framebuffer; // GL_DRAW_FRAMEBUFFER_BINDING
        GLuint vertex_array; // GL_VERTEX_ARRAY_BINDING
        GLuint vertex_buffer; // GL_ARRAY_BUFFER_BINDING
        GLuint uniform_buffer; // GL_UNIFORM_BUFFER_BINDING
        GLuint shader_program; // GL_CURRENT_PROGRAM
    } draw;
};
