// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <glad/glad.h>
#include <memory>

class OGLTexture;
class OGLSampler;
class OGLShader;
class OGLBuffer;
class OGLVertexArray;
class OGLFramebuffer;

class OpenGLState {
public:
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
        GLuint write_mask; // GL_STENCIL_WRITEMASK
        GLenum action_stencil_fail; // GL_STENCIL_FAIL
        GLenum action_depth_fail; // GL_STENCIL_PASS_DEPTH_FAIL
        GLenum action_depth_pass; // GL_STENCIL_PASS_DEPTH_PASS
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
        std::weak_ptr<OGLTexture> texture_2d; // GL_TEXTURE_BINDING_2D
        std::weak_ptr<OGLSampler> sampler; // GL_SAMPLER_BINDING
    } texture_units[3];

    struct {
        std::weak_ptr<OGLTexture> texture_1d; // GL_TEXTURE_BINDING_1D
    } lighting_luts[6];

    struct {
        std::weak_ptr<OGLFramebuffer> read_framebuffer; // GL_READ_FRAMEBUFFER_BINDING
        std::weak_ptr<OGLFramebuffer> draw_framebuffer; // GL_DRAW_FRAMEBUFFER_BINDING
        std::weak_ptr<OGLVertexArray> vertex_array; // GL_VERTEX_ARRAY_BINDING
        std::weak_ptr<OGLBuffer> vertex_buffer; // GL_ARRAY_BUFFER_BINDING
        std::weak_ptr<OGLBuffer> uniform_buffer; // GL_UNIFORM_BUFFER_BINDING
        std::weak_ptr<OGLShader> shader_program; // GL_CURRENT_PROGRAM
    } draw;

    OpenGLState();

    /// Get the currently active OpenGL state
    static const OpenGLState& GetCurState() {
        return cur_state;
    }

    /// Apply this state as the current OpenGL state
    void Apply() const;

    /// Check the status of the current OpenGL read or draw framebuffer configuration
    static GLenum CheckFBStatus(GLenum target);

    /// Resets and unbinds any references to the given resource in the current OpenGL state
    static void ResetTexture(const OGLTexture* texture);
    static void ResetSampler(const OGLSampler* sampler);
    static void ResetProgram(const OGLShader* program);
    static void ResetBuffer(const OGLBuffer* buffer);
    static void ResetVertexArray(const OGLVertexArray* vertex_array);
    static void ResetFramebuffer(const OGLFramebuffer* framebuffer);

private:
    static OpenGLState cur_state;

    static struct ResourceHandles {
        struct {
            GLuint texture_2d;
            GLuint sampler;
        } texture_units[3];

        struct {
            GLuint texture_1d;
        } lighting_luts[6];

        struct {
            GLuint read_framebuffer;
            GLuint draw_framebuffer;
            GLuint vertex_array;
            GLuint vertex_buffer;
            GLuint uniform_buffer;
            GLuint shader_program;
        } draw;
    } cur_res_handles;
};
