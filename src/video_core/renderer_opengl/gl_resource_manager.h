// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/resource_manager_base.h"
#include "video_core/pica.h"

#include <set>

class ResourceManagerOpenGL : public ResourceManagerBase {
public:

    ResourceManagerOpenGL();
    ~ResourceManagerOpenGL() override;

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
