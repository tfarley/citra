// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/emu_window.h"
#include "video_core/vertex_shader.h"

class HWRasterizer {
public:
    virtual ~HWRasterizer() {
    }

    /// Initialize API-specific GPU objects
    virtual void InitObjects() = 0;

    /// Set the window (context) to draw with
    virtual void SetWindow(EmuWindow* window) = 0;

    /// Converts the triangle verts to hardware data format and adds them to the current batch
    virtual void AddTriangle(const Pica::VertexShader::OutputVertex& v0,
                             const Pica::VertexShader::OutputVertex& v1,
                             const Pica::VertexShader::OutputVertex& v2) = 0;

    /// Draw the current batch of triangles
    virtual void DrawTriangles() = 0;

    /// Notify rasterizer that a copy within 3DS memory will occur after this notification
    virtual void NotifyPreCopy(u32 src_paddr, u32 size) = 0;

    /// Notify rasterizer that a 3DS memory region has been changed
    virtual void NotifyFlush(u32 paddr, u32 size) = 0;
};
