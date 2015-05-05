// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/hwrasterizer_base.h"

#include "gl_rasterizer_cache.h"

class RasterizerOpenGL : public HWRasterizer {
public:

    RasterizerOpenGL(ResourceManagerOpenGL* res_mgr);
    ~RasterizerOpenGL();

    /// Initialize API-specific GPU objects
    void InitObjects();

    /// Set the window (context) to draw with
    void SetWindow(EmuWindow* window);

    /// Sets the state to return to for rendering
    void SetBaseState(GLuint orig_vao);

    /// Converts the triangle verts to hardware data format and adds them to the current batch
    void AddTriangle(const Pica::VertexShader::OutputVertex& v0,
                     const Pica::VertexShader::OutputVertex& v1,
                     const Pica::VertexShader::OutputVertex& v2);

    /// Draw the current batch of triangles
    void DrawTriangles();

    /// Notify renderer that a frame is about to draw
    void NotifyPreSwapBuffers();

    /// Notify renderer that a copy is about to happen
    void NotifyPreCopy(u32 src_addr, u32 src_size, u32 dest_addr, u32 dest_size);

    /// Notify renderer that memory region has been changed
    void NotifyFlush(bool is_phys_addr, u32 addr, u32 size);

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

    /// Structure used for storing information about color textures
    struct TextureInfo {
        GLuint handle;
        GLsizei width;
        GLsizei height;
        GPU::Regs::PixelFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    /// Structure used for storing information about depth textures
    struct DepthTextureInfo {
        GLuint handle;
        GLsizei width;
        GLsizei height;
        Pica::Regs::DepthFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    ///Structure that the hardware rendered vertices are composed of
    struct HardwareVertex {
        HardwareVertex(const Pica::VertexShader::OutputVertex& v) {
            position[0] = v.pos.x.ToFloat32();
            position[1] = v.pos.y.ToFloat32();
            position[2] = v.pos.z.ToFloat32();
            position[3] = v.pos.w.ToFloat32();
            color[0] = v.color.x.ToFloat32();
            color[1] = v.color.y.ToFloat32();
            color[2] = v.color.z.ToFloat32();
            color[3] = v.color.w.ToFloat32();
            tex_coord0[0] = v.tc0.x.ToFloat32();
            tex_coord0[1] = v.tc0.y.ToFloat32();
            tex_coord1[0] = v.tc1.x.ToFloat32();
            tex_coord1[1] = v.tc1.y.ToFloat32();
            tex_coord2[0] = v.tc2.x.ToFloat32();
            tex_coord2[1] = v.tc2.y.ToFloat32();
        }

        GLfloat position[4];
        GLfloat color[4];
        GLfloat tex_coord0[2];
        GLfloat tex_coord1[2];
        GLfloat tex_coord2[2];
    };

    void ReconfigColorTexture(TextureInfo& texture, GPU::Regs::PixelFormat format, u32 width, u32 height);
    void ReconfigDepthTexture(DepthTextureInfo& texture, Pica::Regs::DepthFormat format, u32 width, u32 height);

    /// Syncs the state and contents of the OpenGL framebuffer with the current PICA framebuffer
    void SyncFramebuffer();

    /// Syncs the OpenGL drawing state with the current PICA state
    void SyncDrawState();

    /// Saves OpenGL state
    void SaveRendererState();

    /// Restores state so renderer can draw textures to screen
    void RestoreRendererState();

    /// Copies the 3ds color framebuffer into the OpenGL color framebuffer texture
    void ReloadColorBuffer();

    /// Copies the 3ds depth framebuffer into the OpenGL depth framebuffer texture
    void ReloadDepthBuffer();

    /**
    * Save the current OpenGL framebuffer to the current PICA framebuffer in 3ds memory
    * Loads the OpenGL framebuffer textures into temporary buffers
    * Then copies into the 3ds framebuffer using proper Morton order
    */
    void CommitFramebuffer();

    EmuWindow* render_window;
    ResourceManagerOpenGL* res_mgr;
    RasterizerCacheOpenGL res_cache;

    std::vector<HardwareVertex> vertex_batch;

    bool did_init;
    bool needs_state_reinit;

    GLuint old_vao;
    GLuint old_shader;

    u32 last_fb_color_addr;
    u32 last_fb_depth_addr;

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
