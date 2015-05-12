// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"

#include "video_core/pica.h"

#include "generated/gl_3_2_core.h"

namespace PicaToGL {

static GLenum WrapMode(Pica::Regs::TextureConfig::WrapMode mode) {
    switch (mode) {
    case Pica::Regs::TextureConfig::WrapMode::ClampToEdge:
        return GL_CLAMP_TO_EDGE;
    case Pica::Regs::TextureConfig::WrapMode::Repeat:
        return GL_REPEAT;
    case Pica::Regs::TextureConfig::WrapMode::MirroredRepeat:
        return GL_MIRRORED_REPEAT;
    default:
        LOG_CRITICAL(Render_OpenGL, "Unknown texture wrap mode %d", mode);
        UNIMPLEMENTED();
        return GL_CLAMP_TO_EDGE;
    }
}

static GLenum BlendFunc(u32 factor) {
    switch (factor) {
    case Pica::registers.output_merger.alpha_blending.Zero:
        return GL_ZERO;
    case Pica::registers.output_merger.alpha_blending.One:
        return GL_ONE;
    case Pica::registers.output_merger.alpha_blending.SourceColor:
        return GL_SRC_COLOR;
    case Pica::registers.output_merger.alpha_blending.OneMinusSourceColor:
        return GL_ONE_MINUS_SRC_COLOR;
    case Pica::registers.output_merger.alpha_blending.DestColor:
        return GL_DST_COLOR;
    case Pica::registers.output_merger.alpha_blending.OneMinusDestColor:
        return GL_ONE_MINUS_DST_COLOR;
    case Pica::registers.output_merger.alpha_blending.SourceAlpha:
        return GL_SRC_ALPHA;
    case Pica::registers.output_merger.alpha_blending.OneMinusSourceAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case Pica::registers.output_merger.alpha_blending.DestAlpha:
        return GL_DST_ALPHA;
    case Pica::registers.output_merger.alpha_blending.OneMinusDestAlpha:
        return GL_ONE_MINUS_DST_ALPHA;
    case Pica::registers.output_merger.alpha_blending.ConstantColor:
        return GL_CONSTANT_COLOR;
    case Pica::registers.output_merger.alpha_blending.OneMinusConstantColor:
        return GL_ONE_MINUS_CONSTANT_COLOR;
    case Pica::registers.output_merger.alpha_blending.ConstantAlpha:
        return GL_CONSTANT_ALPHA;
    case Pica::registers.output_merger.alpha_blending.OneMinusConstantAlpha:
        return GL_ONE_MINUS_CONSTANT_ALPHA;
    case Pica::registers.output_merger.alpha_blending.SourceAlphaSaturate:
        return GL_SRC_ALPHA_SATURATE;
    default:
        LOG_CRITICAL(Render_OpenGL, "Unknown blend factor %d", factor);
        UNIMPLEMENTED();
        return GL_ONE;
    }
}

static GLenum CompareFunc(u32 func) {
    switch (func) {
    case Pica::registers.output_merger.Never:
        return GL_NEVER;
    case Pica::registers.output_merger.Always:
        return GL_ALWAYS;
    case Pica::registers.output_merger.Equal:
        return GL_EQUAL;
    case Pica::registers.output_merger.NotEqual:
        return GL_NOTEQUAL;
    case Pica::registers.output_merger.LessThan:
        return GL_LESS;
    case Pica::registers.output_merger.LessThanOrEqual:
        return GL_LEQUAL;
    case Pica::registers.output_merger.GreaterThan:
        return GL_GREATER;
    case Pica::registers.output_merger.GreaterThanOrEqual:
        return GL_GEQUAL;
    default:
        LOG_CRITICAL(Render_OpenGL, "Unknown compare function %d", func);
        UNIMPLEMENTED();
        return GL_ALWAYS;
    }
}

} // namespace
