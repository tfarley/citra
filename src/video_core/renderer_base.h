// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common.h"

class RendererBase : NonCopyable {
public:

    /// Used to reference a framebuffer
    enum kFramebuffer {
        kFramebuffer_VirtualXFB = 0,
        kFramebuffer_EFB,
        kFramebuffer_Texture
    };

    RendererBase() : m_current_fps(0), m_current_frame(0) {
    }

    virtual ~RendererBase() {
    }

    /// Swap buffers (render frame)
    virtual void SwapBuffers() = 0;

    /**
     * Set the emulator window to use for renderer
     * @param window EmuWindow handle to emulator window to use for rendering
     */
    virtual void SetWindow(EmuWindow* window) = 0;

    /// Initialize the renderer
    virtual void Init() = 0;

    /// Shutdown the renderer
    virtual void ShutDown() = 0;

    /// Draw a batch of triangles
    virtual void DrawBatch(bool is_indexed) = 0;

    /// Notify renderer that memory region has been changed
    virtual void NotifyFlush(bool is_phys_addr, u32 addr, u32 size) = 0;

    /// Notify renderer that a display transfer is about to happen
    virtual void NotifyPreDisplayTransfer(u32 src_addr, u32 dest_addr) = 0;

    // Getter/setter functions:
    // ------------------------

    f32 GetCurrentframe() const {
        return m_current_fps;
    }

    int current_frame() const {
        return m_current_frame;
    }

protected:
    f32 m_current_fps;              ///< Current framerate, should be set by the renderer
    int m_current_frame;            ///< Current frame, should be set by the renderer

};
