// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "gl_rasterizer_cache.h"

class RasterizerOpenGL {
public:

    RasterizerOpenGL(ResourceManagerOpenGL* res_mgr);
    virtual ~RasterizerOpenGL();

    /// Draw a batch of triangles
    void DrawBatch(bool is_indexed);

    void NotifySwapBuffers();

    void NotifyFlush(bool is_phys_addr, u32 addr, u32 size);

    void CommitFramebuffer();

private:
    /// Structure used for managing texture environment states
    struct TEVUniforms {
        GLuint color_src;
        GLuint alpha_src;
        GLuint color_mod;
        GLuint alpha_mod;
        GLuint color_op;
        GLuint alpha_op;
        GLuint const_color;
    };

    /// Structure used for storing information about some textures
    struct TextureInfo {
        GLuint handle;
        GLsizei width;
        GLsizei height;
        GPU::Regs::PixelFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    void ReconfigureTexture(TextureInfo& texture, GPU::Regs::PixelFormat format, u32 width, u32 height);

    void SetupDrawState();

    void ReloadColorBuffer();
    void ReloadDepthBuffer();

    ResourceManagerOpenGL* res_mgr;

    RasterizerCacheOpenGL* res_cache;

    bool needs_state_reinit;

    u32 last_fb_color_addr;
    u32 last_fb_color_format;
    u32 last_fb_depth_addr;
    u32 last_fb_depth_format;

    // Hardware rasterizer
    TextureInfo fb_color_texture;
    TextureInfo fb_depth_texture;
    GLuint shader_handle;
    GLuint vertex_array_handle;
    GLuint vertex_buffer_handle;
    GLuint framebuffer_handle;

    // Hardware vertex shader
    GLuint attrib_position;
    GLuint attrib_color;
    GLuint attrib_texcoords;

    // Hardware fragment shader
    GLuint uniform_alphatest_func;
    GLuint uniform_alphatest_ref;
    GLuint uniform_tex;
    TEVUniforms uniform_tevs[6];
    GLuint uniform_out_maps;
    GLuint uniform_tex_envs;
};
