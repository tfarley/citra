// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_shader_util.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/math.h"

ResourceManagerOpenGL::ResourceManagerOpenGL() {
}

ResourceManagerOpenGL::~ResourceManagerOpenGL() {
    for (auto texture_handle : texture_handles) {
        glDeleteTextures(1, &texture_handle);
    }

    for (auto shader_handle : shader_handles) {
        glDeleteProgram(shader_handle);
    }

    for (auto buffer_handle : buffer_handles) {
        glDeleteBuffers(1, &buffer_handle);
    }

    for (auto vao_handle : vao_handles) {
        glDeleteVertexArrays(1, &vao_handle);
    }

    for (auto framebuffer_handle : framebuffer_handles) {
        glDeleteFramebuffers(1, &framebuffer_handle);
    }
}

GLuint ResourceManagerOpenGL::NewTexture() {
    GLuint new_handle;
    glGenTextures(1, &new_handle);

    texture_handles.insert(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteTexture(GLuint handle) {
    auto it = texture_handles.find(handle);
    if (it != texture_handles.end()) {
        glDeleteTextures(1, &handle);
        texture_handles.erase(it);
    }
}

GLuint ResourceManagerOpenGL::NewShader(const char *vert_shader, const char *frag_shader) {
    GLuint new_handle = ShaderUtil::LoadShaders(vert_shader, frag_shader);

    shader_handles.insert(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteShader(GLuint handle) {
    auto it = shader_handles.find(handle);
    if (it != shader_handles.end()) {
        glDeleteProgram(handle);
        shader_handles.erase(it);
    }
}

GLuint ResourceManagerOpenGL::NewBuffer() {
    GLuint new_handle;
    glGenBuffers(1, &new_handle);

    buffer_handles.insert(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteBuffer(GLuint handle) {
    auto it = buffer_handles.find(handle);
    if (it != buffer_handles.end()) {
        glDeleteBuffers(1, &handle);
        buffer_handles.erase(it);
    }
}

GLuint ResourceManagerOpenGL::NewVAO() {
    GLuint new_handle;
    glGenVertexArrays(1, &new_handle);

    vao_handles.insert(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteVAO(GLuint handle) {
    auto it = vao_handles.find(handle);
    if (it != vao_handles.end()) {
        glDeleteVertexArrays(1, &handle);
        vao_handles.erase(it);
    }
}

GLuint ResourceManagerOpenGL::NewFramebuffer() {
    GLuint new_handle;
    glGenFramebuffers(1, &new_handle);

    framebuffer_handles.insert(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteFramebuffer(GLuint handle) {
    auto it = framebuffer_handles.find(handle);
    if (it != framebuffer_handles.end()) {
        glDeleteFramebuffers(1, &handle);
        framebuffer_handles.erase(it);
    }
}
