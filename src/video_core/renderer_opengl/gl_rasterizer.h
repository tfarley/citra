// Copyright 2015 Citra Emulator Project
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

    void NotifyPreSwapBuffers();

    /// Notify renderer that memory region has been changed
    void NotifyFlush(bool is_phys_addr, u32 addr, u32 size);

    /**
     * Save the current OpenGL framebuffer to the current PICA framebuffer in 3ds memory
     * Loads the OpenGL framebuffer textures into temporary buffers
     * Then copies into the 3ds framebuffer using proper Morton order
     */
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

    /// Structure used for storing information about some depth textures
    struct DepthTextureInfo {
        GLuint handle;
        GLsizei width;
        GLsizei height;
        Pica::Regs::DepthFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    void ReconfigColorTexture(TextureInfo& texture, GPU::Regs::PixelFormat format, u32 width, u32 height);
    void ReconfigDepthTexture(DepthTextureInfo& texture, Pica::Regs::DepthFormat format, u32 width, u32 height);

    /// Syncs the state and contents of the OpenGL framebuffer with the current PICA framebuffer
    void SyncFramebuffer();

    /// Syncs the OpenGL drawing state with the current PICA state
    void SyncDrawState();

    /// Copies the 3ds color framebuffer into the OpenGL color framebuffer texture
    void ReloadColorBuffer();

    /// Copies the 3ds depth framebuffer into the OpenGL depth framebuffer texture
    void ReloadDepthBuffer();

    ResourceManagerOpenGL* res_mgr;

    RasterizerCacheOpenGL* res_cache;

    bool needs_state_reinit;

    u32 last_fb_color_addr;
    GPU::Regs::PixelFormat last_fb_color_format;
    u32 last_fb_depth_addr;
    Pica::Regs::DepthFormat last_fb_depth_format;

    // Hardware rasterizer
    TextureInfo fb_color_texture;
    DepthTextureInfo fb_depth_texture;
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
