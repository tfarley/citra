// Copyright 2014 Citra Emulator Project
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

    texture_handles.push_back(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteTexture(GLuint handle) {
    for (auto it = texture_handles.begin(); it != texture_handles.end(); ++it) {
        if (*it == handle) {
            glDeleteTextures(1, &(*it));
            texture_handles.erase(it);
            break;
        }
    }
}

GLuint ResourceManagerOpenGL::NewShader(const char *vert_shader, const char *frag_shader) {
    GLuint new_handle = ShaderUtil::LoadShaders(vert_shader, frag_shader);

    shader_handles.push_back(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteShader(GLuint handle) {
    for (auto it = shader_handles.begin(); it != shader_handles.end(); ++it) {
        if (*it == handle) {
            glDeleteProgram(*it);
            shader_handles.erase(it);
            break;
        }
    }
}

GLuint ResourceManagerOpenGL::NewBuffer() {
    GLuint new_handle;
    glGenBuffers(1, &new_handle);

    buffer_handles.push_back(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteBuffer(GLuint handle) {
    for (auto it = buffer_handles.begin(); it != buffer_handles.end(); ++it) {
        if (*it == handle) {
            glDeleteBuffers(1, &(*it));
            buffer_handles.erase(it);
            break;
        }
    }
}

GLuint ResourceManagerOpenGL::NewVAO() {
    GLuint new_handle;
    glGenVertexArrays(1, &new_handle);

    vao_handles.push_back(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteVAO(GLuint handle) {
    for (auto it = vao_handles.begin(); it != vao_handles.end(); ++it) {
        if (*it == handle) {
            glDeleteVertexArrays(1, &(*it));
            vao_handles.erase(it);
            break;
        }
    }
}

GLuint ResourceManagerOpenGL::NewFramebuffer() {
    GLuint new_handle;
    glGenFramebuffers(1, &new_handle);

    framebuffer_handles.push_back(new_handle);

    return new_handle;
}

void ResourceManagerOpenGL::DeleteFramebuffer(GLuint handle) {
    for (auto it = framebuffer_handles.begin(); it != framebuffer_handles.end(); ++it) {
        if (*it == handle) {
            glDeleteFramebuffers(1, &(*it));
            framebuffer_handles.erase(it);
            break;
        }
    }
}
