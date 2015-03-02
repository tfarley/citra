// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>

#include "generated/gl_3_2_core.h"

#include "common/math_util.h"

#include "core/hw/gpu.h"

#include "video_core/renderer_base.h"

#define USE_OGL_RENDERER

class EmuWindow;

struct RawVertex {
    RawVertex() = default;

    float attribs[8][4];
};

class RendererOpenGL : public RendererBase {
public:

    RendererOpenGL();
    ~RendererOpenGL() override;

    /// Swap buffers (render frame)
    void SwapBuffers() override;

    /**
     * Set the emulator window to use for renderer
     * @param window EmuWindow handle to emulator window to use for rendering
     */
    void SetWindow(EmuWindow* window) override;

    /// Initialize the renderer
    void Init() override;

    /// Shutdown the renderer
    void ShutDown() override;

    void BeginBatch();
    void DrawTriangle(const RawVertex& v0, const RawVertex& v1, const RawVertex& v2);
    void EndBatch();

    void SetUniformBool(u32 index, int value);
    void SetUniformInts(u32 index, const u32* values);
    void SetUniformFloats(u32 index, const float* values);

private:
    /// Structure used for storing information about the textures for each 3DS screen
    struct TextureInfo {
        GLuint handle;
        GLsizei width;
        GLsizei height;
        GPU::Regs::PixelFormat format;
        GLenum gl_format;
        GLenum gl_type;
    };

    void InitOpenGLObjects();
    static void ConfigureFramebufferTexture(TextureInfo& texture,
                                            const GPU::Regs::FramebufferConfig& framebuffer);
    void DrawScreens();
    void DrawSingleScreenRotated(const TextureInfo& texture, float x, float y, float w, float h);
    void UpdateFramerate();

    // Loads framebuffer from emulated memory into the active OpenGL texture.
    static void LoadFBToActiveGLTexture(const GPU::Regs::FramebufferConfig& framebuffer,
                                        const TextureInfo& texture);

    /// Computes the viewport rectangle
    MathUtil::Rectangle<unsigned> GetViewportExtent();

    EmuWindow*  render_window;                    ///< Handle to render window
    u32         last_mode;                        ///< Last render mode

    int resolution_width;                         ///< Current resolution width
    int resolution_height;                        ///< Current resolution height

    // OpenGL object IDs
    GLuint vertex_array_handle;
    GLuint vertex_buffer_handle;
    GLuint program_id;
    std::array<TextureInfo, 2> textures;
    // Shader uniform location indices
    GLuint uniform_modelview_matrix;
    GLuint uniform_color_texture;
    // Shader attribute input indices
    GLuint attrib_position;
    GLuint attrib_tex_coord;
    // Hardware renderer
    GLuint hw_vertex_array_handle;
    GLuint hw_vertex_buffer_handle;
    GLuint attrib_v;
    GLuint uniform_c;
    GLuint uniform_b;
    GLuint uniform_i;
    GLuint uniform_tex;
};
