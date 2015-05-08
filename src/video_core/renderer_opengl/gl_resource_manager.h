// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/pica.h"

#include "generated/gl_3_2_core.h"

#include <set>

class ResourceManagerOpenGL : NonCopyable {
public:

    ResourceManagerOpenGL();
    ~ResourceManagerOpenGL();

    GLuint NewTexture();
    void DeleteTexture(GLuint handle);

    GLuint NewShader(const char *vert_shader, const char *frag_shader);
    void DeleteShader(GLuint handle);

    GLuint NewBuffer();
    void DeleteBuffer(GLuint handle);

    GLuint NewVAO();
    void DeleteVAO(GLuint handle);

    GLuint NewFramebuffer();
    void DeleteFramebuffer(GLuint handle);

private:
    std::set<GLuint> texture_handles;
    std::set<GLuint> shader_handles;
    std::set<GLuint> buffer_handles;
    std::set<GLuint> vao_handles;
    std::set<GLuint> framebuffer_handles;
};
