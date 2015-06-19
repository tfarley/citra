// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "gl_state.h"
#include "gl_resource_manager.h"
#include "video_core/pica.h"

#include <memory>
#include <map>

class RasterizerCacheOpenGL : NonCopyable {
public:
    ~RasterizerCacheOpenGL();

    /// Structure used for storing information about color textures
    struct TextureInfo {
        GLsizei width;
        GLsizei height;
        Pica::Regs::ColorFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    /// Structure used for storing information about depth textures
    struct DepthTextureInfo {
        GLsizei width;
        GLsizei height;
        Pica::Regs::DepthFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    /// Loads a texture from 3DS memory to OpenGL and caches it (if not already cached)
    void LoadAndBindTexture(OpenGLState& state, unsigned texture_unit, const Pica::Regs::FullTextureConfig& config);

    bool LoadAndBindShader(bool force_reload, OpenGLState& state, u32 main_offset, const u32* shader_data, const u32* swizzle_data);

    void LoadAndBindFramebufferTexture(OpenGLState& state, unsigned texture_unit, PAddr addr, bool is_depth);

    GLuint GetFramebufferTextureHandle(PAddr addr, bool is_depth);

    TextureInfo& GetFramebufferColorTextureInfo(PAddr addr);
    DepthTextureInfo& GetFramebufferDepthTextureInfo(PAddr addr);

    void SetCopyMap(PAddr src_addr, PAddr dst_addr);

    /// Flush any cached resource that touches the flushed region
    void NotifyFlush(PAddr addr, u32 size);

    /// Flush all cached OpenGL resources tracked by this cache manager
    void FullFlush();

private:
    struct CachedTexture {
        OGLTexture texture;
        GLuint width;
        GLuint height;
        u32 size;
    };

    std::map<PAddr, std::unique_ptr<CachedTexture>> texture_cache;
    std::map<std::string, std::unique_ptr<OGLShader>> vertex_shader_cache;

    std::map<PAddr, std::unique_ptr<OGLTexture>> fb_color_texture_cache;
    std::map<PAddr, std::unique_ptr<OGLTexture>> fb_depth_texture_cache;

    std::map<PAddr, PAddr> fb_texture_copy_map;

    std::map<PAddr, TextureInfo> fb_color_texture_info;
    std::map<PAddr, DepthTextureInfo> fb_depth_texture_info;

    std::string cur_shader_key;
};
