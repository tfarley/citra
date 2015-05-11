// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include "generated/gl_3_2_core.h"

class OGLResource : NonCopyable {
public:
    OGLResource();
    virtual ~OGLResource();

    /// Returns the internal OpenGL resource handle for this resource
    inline GLuint GetHandle() {
        return handle;
    }

    /// Deletes the internal OpenGL resource
    virtual void Release();

protected:
    GLuint handle;
};

class OGLTexture : public OGLResource {
public:
    ~OGLTexture() override;

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release() override;
};

class OGLShader : public OGLResource {
public:
    ~OGLShader() override;

    /// Creates a new internal OpenGL resource and stores the handle
    void Create(const char* vert_shader, const char* frag_shader);

    /// Deletes the internal OpenGL resource
    void Release() override;
};

class OGLBuffer : public OGLResource {
public:
    ~OGLBuffer() override;

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release() override;
};

class OGLVertexArray : public OGLResource {
public:
    ~OGLVertexArray() override;

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release() override;
};

class OGLFramebuffer : public OGLResource {
public:
    ~OGLFramebuffer() override;

    /// Creates a new internal OpenGL resource and stores the handle
    void Create();

    /// Deletes the internal OpenGL resource
    void Release() override;
};
