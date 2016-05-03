// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <tuple>

#include <glad/glad.h>

class OpenGLState {
public:
    OpenGLState();
    ~OpenGLState();

    /// Get a pointer to the currently bound state tracker object
    static OpenGLState* GetCurrentState();

    /// Apply this state as the current OpenGL state
    void MakeCurrent();

    /// Getter and setter functions for OpenGL state
    inline bool GetCullEnabled() { return cull.enabled; }
    void SetCullEnabled(bool n_enabled);
    inline GLenum GetCullMode() { return cull.mode; }
    void SetCullMode(GLenum n_mode);
    inline GLenum GetCullFrontFace() { return cull.front_face; }
    void SetCullFrontFace(GLenum n_front_face);

    inline bool GetDepthTestEnabled() { return depth.test_enabled; }
    void SetDepthTestEnabled(bool n_test_enabled);
    inline GLenum GetDepthFunc() { return depth.test_func; }
    void SetDepthFunc(GLenum n_test_func);
    inline GLboolean GetDepthWriteMask() { return depth.write_mask; }
    void SetDepthWriteMask(GLboolean n_write_mask);

    inline std::tuple<GLboolean, GLboolean, GLboolean, GLboolean> GetColorMask() {
        return std::make_tuple(color_mask.red_enabled, color_mask.green_enabled, color_mask.blue_enabled, color_mask.alpha_enabled);
    }
    void SetColorMask(std::tuple<GLboolean, GLboolean, GLboolean, GLboolean> n_rgba_enabled);

    inline bool GetStencilTestEnabled() { return stencil.test_enabled; }
    void SetStencilTestEnabled(bool n_test_enabled);
    inline std::tuple<GLenum, GLint, GLint> GetStencilFunc() {
        return std::make_tuple(stencil.test_func, stencil.test_ref, stencil.test_mask);
    }
    void SetStencilFunc(std::tuple<GLenum, GLint, GLint> n_funcs);
    inline std::tuple<GLenum, GLenum, GLenum> GetStencilOp() {
        return std::make_tuple(stencil.action_stencil_fail, stencil.action_depth_fail, stencil.action_depth_pass);
    }
    void SetStencilOp(std::tuple<GLenum, GLenum, GLenum> n_actions);
    inline GLuint GetStencilWriteMask() { return stencil.write_mask; }
    void SetStencilWriteMask(GLuint n_write_mask);

    inline bool GetBlendEnabled() { return blend.enabled; }
    void SetBlendEnabled(bool n_enabled);
    inline std::tuple<GLenum, GLenum, GLenum, GLenum> GetBlendFunc() {
        return std::make_tuple(blend.src_rgb_func, blend.dst_rgb_func, blend.src_a_func, blend.dst_a_func);
    }
    void SetBlendFunc(std::tuple<GLenum, GLenum, GLenum, GLenum> n_funcs);
    inline std::tuple<GLclampf, GLclampf, GLclampf, GLclampf> GetBlendColor() {
        return std::make_tuple(blend.color.red, blend.color.green, blend.color.blue, blend.color.alpha);
    }
    void SetBlendColor(std::tuple<GLclampf, GLclampf, GLclampf, GLclampf> n_color);

    inline GLenum GetLogicOp() { return logic_op; }
    void SetLogicOp(GLenum n_logic_op);

    inline GLuint GetTexture1D() { return texture_units[active_texture_unit - GL_TEXTURE0].texture_1d; }
    void SetTexture1D(GLuint n_texture_1d);
    inline GLuint GetTexture2D() { return texture_units[active_texture_unit - GL_TEXTURE0].texture_2d; }
    void SetTexture2D(GLuint n_texture_2d);
    inline GLuint GetSampler() { return texture_units[active_texture_unit - GL_TEXTURE0].sampler; }
    void SetSampler(GLuint n_sampler);

    inline GLenum GetActiveTextureUnit() { return active_texture_unit; }
    void SetActiveTextureUnit(GLenum n_active_texture_unit);

    inline GLuint GetReadFramebuffer() { return draw.read_framebuffer; }
    void SetReadFramebuffer(GLuint n_read_framebuffer);
    inline GLuint GetDrawFramebuffer() { return draw.draw_framebuffer; }
    void SetDrawFramebuffer(GLuint n_draw_framebuffer);
    inline GLuint GetVertexArray() { return draw.vertex_array; }
    void SetVertexArray(GLuint n_vertex_array);
    inline GLuint GetVertexBuffer() { return draw.vertex_buffer; }
    void SetVertexBuffer(GLuint n_vertex_buffer);
    inline GLuint GetUniformBuffer() { return draw.uniform_buffer; }
    void SetUniformBuffer(GLuint n_uniform_buffer);
    inline GLuint GetShaderProgram() { return draw.shader_program; }
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

    /// Manipulates the current state to prepare for pixel transfer, and returns a copy of the old state
    static OpenGLState ApplyTransferState(GLuint src_tex, GLuint read_framebuffer, GLuint dst_tex, GLuint draw_framebuffer);

    /// Returns the old state before transfer state changes were made
    static void UndoTransferState(OpenGLState &old_state);

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
