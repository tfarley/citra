// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "gl_state.h"
#include "gl_resource_manager.h"
#include "video_core/pica.h"

#include <map>

class RasterizerCacheOpenGL : NonCopyable {
public:

    RasterizerCacheOpenGL(ResourceManagerOpenGL* res_mgr);
    ~RasterizerCacheOpenGL();

    /// Loads a texture from 3ds to OpenGL and caches it (if not already cached)
    void LoadAndBindTexture(OpenGLState &state, int texture_unit, const Pica::Regs::FullTextureConfig& config);

    /// Flush any cached resource that touches the flushed region
    void NotifyFlush(u32 paddr, u32 size);

    /// Flush all cached OpenGL resources tracked by this cache manager
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
