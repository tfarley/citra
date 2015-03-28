// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "primitive_assembly.h"

namespace Pica {

namespace VertexProcessor {

/// Runs the current PICA vertex batch through the shader interpreter, and sends resulting primitives to triangle_handler
void ProcessBatch(bool is_indexed, PrimitiveAssembler<VertexShader::OutputVertex>::TriangleHandler triangle_handler);

} // namespace

} // namespace

