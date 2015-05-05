// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "gl_resource_manager.h"
#include "video_core/pica.h"

#include <map>

class RasterizerCacheOpenGL {
public:

    RasterizerCacheOpenGL(ResourceManagerOpenGL* res_mgr);
    virtual ~RasterizerCacheOpenGL();

    /// Loads a texture from 3ds to OpenGL and caches it (if not already cached)
    void LoadAndBindTexture(const Pica::Regs::FullTextureConfig& config);

    /// Flush any cached resource that touches the flushed region
    void NotifyFlush(bool is_phys_addr, u32 addr, u32 size);

    /// Flush all cached resources
    void FullFlush();

private:
    struct CachedTexture {
        GLuint handle;
        GLuint width;
        GLuint height;
        u32 size;
    };

    ResourceManagerOpenGL* res_mgr;

    std::map<u32, CachedTexture> texture_cache;
};
