// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/mem_map.h"
#include "video_core/renderer_opengl/gl_pica_to_gl.h"
#include "video_core/renderer_opengl/gl_rasterizer_cache.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/math.h"

RasterizerCacheOpenGL::~RasterizerCacheOpenGL() {
    FullFlush();
}

void RasterizerCacheOpenGL::LoadAndBindTexture(OpenGLState &state, int texture_unit, const Pica::Regs::FullTextureConfig& config) {
    PAddr texture_addr = config.config.GetPhysicalAddress();

    const auto cached_texture = texture_cache.find(texture_addr);

    if (cached_texture != texture_cache.end()) {
        state.texture_units[texture_unit].texture_2d = cached_texture->second->texture.GetHandle();
        state.Apply();
    } else {
        std::unique_ptr<CachedTexture> new_texture(new CachedTexture());

        new_texture->texture.Create();
        state.texture_units[texture_unit].texture_2d = new_texture->texture.GetHandle();
        state.Apply();

        // TODO: Need to choose filters that correspond to PICA once register is declared
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, PicaToGL::WrapMode(config.config.wrap_s.Value()));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, PicaToGL::WrapMode(config.config.wrap_t.Value()));

        const auto info = Pica::DebugUtils::TextureInfo::FromPicaRegister(config.config, config.format);

        new_texture->width = info.width;
        new_texture->height = info.height;
        new_texture->size = info.width * info.height * Pica::Regs::NibblesPerPixel(info.format);

        std::unique_ptr<Math::Vec4<u8>[]> temp_texture_buffer_rgba(new Math::Vec4<u8>[info.width * info.height]);

        for (int y = 0; y < info.height; ++y) {
            for (int x = 0; x < info.width; ++x) {
                temp_texture_buffer_rgba[x + info.width * y] = Pica::DebugUtils::LookupTexture(Memory::GetPhysicalPointer(texture_addr), x, info.height - 1 - y, info);
            }
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, temp_texture_buffer_rgba.get());

        texture_cache.emplace(texture_addr, std::move(new_texture));
    }
}

void RasterizerCacheOpenGL::NotifyFlush(PAddr addr, u32 size) {
    // Flush any texture that falls in the flushed region
    for (auto it = texture_cache.begin(); it != texture_cache.end();) {
        PAddr max_low_addr_bound = std::max(addr, it->first);
        PAddr min_hi_addr_bound = std::min(addr + size, it->first + it->second->size);

        if (max_low_addr_bound <= min_hi_addr_bound) {
            it = texture_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void RasterizerCacheOpenGL::FullFlush() {
    texture_cache.clear();
}
