// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/resource_manager_base.h"
#include "video_core/pica.h"

#include <map>

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
    std::vector<GLuint> texture_handles;
    std::vector<GLuint> shader_handles;
    std::vector<GLuint> buffer_handles;
    std::vector<GLuint> vao_handles;
    std::vector<GLuint> framebuffer_handles;
};
