// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include "generated/gl_3_2_core.h"

class OGLTexture : public NonCopyable {
public:
    OGLTexture();
    ~OGLTexture();

    /// Returns the internal OpenGL resource handle for this resource
    inline GLuint GetHandle() {
        return handle;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

protected:
    GLuint handle;
};

class OGLShader : public NonCopyable {
public:
    OGLShader();
    ~OGLShader();

    /// Returns the internal OpenGL resource handle for this resource
    inline GLuint GetHandle() {
        return handle;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create(const char* vert_shader, const char* frag_shader);

    /// Deletes the internal OpenGL resource
    void Release();

protected:
    GLuint handle;
};

class OGLBuffer : public NonCopyable {
public:
    OGLBuffer();
    ~OGLBuffer();

    /// Returns the internal OpenGL resource handle for this resource
    inline GLuint GetHandle() {
        return handle;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

protected:
    GLuint handle;
};

class OGLVertexArray : public NonCopyable {
public:
    OGLVertexArray();
    ~OGLVertexArray();

    /// Returns the internal OpenGL resource handle for this resource
    inline GLuint GetHandle() {
        return handle;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

protected:
    GLuint handle;
};

class OGLFramebuffer : public NonCopyable {
public:
    OGLFramebuffer();
    ~OGLFramebuffer();

    /// Returns the internal OpenGL resource handle for this resource
    inline GLuint GetHandle() {
        return handle;
    }

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release();

protected:
    GLuint handle;
};
