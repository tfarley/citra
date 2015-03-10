// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "nihstro/shader_bytecode.h"

#include <string>

std::string PICABinToGLSL(const u32* shader_data, const u32* swizzle_data);
