// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "command_processor.h"
#include "video_core.h"
#include "math.h"
#include "pica.h"
#include "vertex_shader.h"
#include "core/hle/service/gsp_gpu.h"
#include "core/hw/gpu.h"

#include "debug_utils/debug_utils.h"

namespace Pica {

Regs registers;

namespace CommandProcessor {

static int float_regs_counter = 0;

static u32 uniform_write_buffer[4];

static inline void WritePicaReg(u32 id, u32 value, u32 mask) {

    if (id >= registers.NumIds())
        return;

    // If we're skipping this frame, only allow trigger IRQ
    if (GPU::g_skip_frame && id != PICA_REG_INDEX(trigger_irq))
        return;

    // TODO: Figure out how register masking acts on e.g. vs_uniform_setup.set_value
    u32 old_value = registers[id];
    registers[id] = (old_value & ~mask) | (value & mask);

    if (g_debug_context)
        g_debug_context->OnEvent(DebugContext::Event::CommandLoaded, reinterpret_cast<void*>(&id));

    DebugUtils::OnPicaRegWrite(id, registers[id]);

    switch(id) {
        // Trigger IRQ
        case PICA_REG_INDEX(trigger_irq):
            GSP_GPU::SignalInterrupt(GSP_GPU::InterruptId::P3D);
            return;

        // It seems like these trigger vertex rendering
        case PICA_REG_INDEX(trigger_draw):
            VideoCore::g_renderer->DrawBatch(false);
            break;

        case PICA_REG_INDEX(trigger_draw_indexed):
            VideoCore::g_renderer->DrawBatch(true);
            break;

        case PICA_REG_INDEX(vs_bool_uniforms):
            for (unsigned i = 0; i < 16; ++i)
                VertexShader::GetBoolUniform(i) = (registers.vs_bool_uniforms.Value() & (1 << i)) != 0;

            break;

        case PICA_REG_INDEX_WORKAROUND(vs_int_uniforms[0], 0x2b1):
        case PICA_REG_INDEX_WORKAROUND(vs_int_uniforms[1], 0x2b2):
        case PICA_REG_INDEX_WORKAROUND(vs_int_uniforms[2], 0x2b3):
        case PICA_REG_INDEX_WORKAROUND(vs_int_uniforms[3], 0x2b4):
        {
            int index = (id - PICA_REG_INDEX_WORKAROUND(vs_int_uniforms[0], 0x2b1));
            auto values = registers.vs_int_uniforms[index];
            VertexShader::GetIntUniform(index) = Math::Vec4<u8>(values.x, values.y, values.z, values.w);
            LOG_TRACE(HW_GPU, "Set integer uniform %d to %02x %02x %02x %02x",
                      index, values.x.Value(), values.y.Value(), values.z.Value(), values.w.Value());
            break;
        }

        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[0], 0x2c1):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[1], 0x2c2):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[2], 0x2c3):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[3], 0x2c4):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[4], 0x2c5):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[5], 0x2c6):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[6], 0x2c7):
        case PICA_REG_INDEX_WORKAROUND(vs_uniform_setup.set_value[7], 0x2c8):
        {
            auto& uniform_setup = registers.vs_uniform_setup;

            // TODO: Does actual hardware indeed keep an intermediate buffer or does
            //       it directly write the values?
            uniform_write_buffer[float_regs_counter++] = value;

            // Uniforms are written in a packed format such that 4 float24 values are encoded in
            // three 32-bit numbers. We write to internal memory once a full such vector is
            // written.
            if ((float_regs_counter >= 4 && uniform_setup.IsFloat32()) ||
                (float_regs_counter >= 3 && !uniform_setup.IsFloat32())) {
                float_regs_counter = 0;

                auto& uniform = VertexShader::GetFloatUniform(uniform_setup.index);

                if (uniform_setup.index > 95) {
                    LOG_ERROR(HW_GPU, "Invalid VS uniform index %d", (int)uniform_setup.index);
                    break;
                }

                // NOTE: The destination component order indeed is "backwards"
                if (uniform_setup.IsFloat32()) {
                    for (auto i : {0,1,2,3})
                        uniform[3 - i] = float24::FromFloat32(*(float*)(&uniform_write_buffer[i]));
                } else {
                    // TODO: Untested
                    uniform.w = float24::FromRawFloat24(uniform_write_buffer[0] >> 8);
                    uniform.z = float24::FromRawFloat24(((uniform_write_buffer[0] & 0xFF)<<16) | ((uniform_write_buffer[1] >> 16) & 0xFFFF));
                    uniform.y = float24::FromRawFloat24(((uniform_write_buffer[1] & 0xFFFF)<<8) | ((uniform_write_buffer[2] >> 24) & 0xFF));
                    uniform.x = float24::FromRawFloat24(uniform_write_buffer[2] & 0xFFFFFF);
                }

                LOG_TRACE(HW_GPU, "Set uniform %x to (%f %f %f %f)", (int)uniform_setup.index,
                          uniform.x.ToFloat32(), uniform.y.ToFloat32(), uniform.z.ToFloat32(),
                          uniform.w.ToFloat32());

                // TODO: Verify that this actually modifies the register!
                uniform_setup.index = uniform_setup.index + 1;
            }
            break;
        }

        // Load shader program code
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[0], 0x2cc):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[1], 0x2cd):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[2], 0x2ce):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[3], 0x2cf):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[4], 0x2d0):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[5], 0x2d1):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[6], 0x2d2):
        case PICA_REG_INDEX_WORKAROUND(vs_program.set_word[7], 0x2d3):
        {
            VertexShader::SubmitShaderMemoryChange(registers.vs_program.offset, value);
            registers.vs_program.offset++;
            break;
        }

        // Load swizzle pattern data
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[0], 0x2d6):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[1], 0x2d7):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[2], 0x2d8):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[3], 0x2d9):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[4], 0x2da):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[5], 0x2db):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[6], 0x2dc):
        case PICA_REG_INDEX_WORKAROUND(vs_swizzle_patterns.set_word[7], 0x2dd):
        {
            VertexShader::SubmitSwizzleDataChange(registers.vs_swizzle_patterns.offset, value);
            registers.vs_swizzle_patterns.offset++;
            break;
        }

        default:
            break;
    }

    if (g_debug_context)
        g_debug_context->OnEvent(DebugContext::Event::CommandProcessed, reinterpret_cast<void*>(&id));
}

static std::ptrdiff_t ExecuteCommandBlock(const u32* first_command_word) {
    const CommandHeader& header = *(const CommandHeader*)(&first_command_word[1]);

    u32* read_pointer = (u32*)first_command_word;

    const u32 write_mask = ((header.parameter_mask & 0x1) ? (0xFFu <<  0) : 0u) |
                           ((header.parameter_mask & 0x2) ? (0xFFu <<  8) : 0u) |
                           ((header.parameter_mask & 0x4) ? (0xFFu << 16) : 0u) |
                           ((header.parameter_mask & 0x8) ? (0xFFu << 24) : 0u);

    WritePicaReg(header.cmd_id, *read_pointer, write_mask);
    read_pointer += 2;

    for (unsigned int i = 1; i < 1+header.extra_data_length; ++i) {
        u32 cmd = header.cmd_id + ((header.group_commands) ? i : 0);
        WritePicaReg(cmd, *read_pointer, write_mask);
        ++read_pointer;
    }

    // align read pointer to 8 bytes
    if ((first_command_word - read_pointer) % 2)
        ++read_pointer;

    return read_pointer - first_command_word;
}

void ProcessCommandList(const u32* list, u32 size) {
    u32* read_pointer = (u32*)list;
    u32 list_length = size / sizeof(u32);

    while (read_pointer < list + list_length) {
        read_pointer += ExecuteCommandBlock(read_pointer);
    }
}

} // namespace

} // namespace
