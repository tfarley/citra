// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "math.h"
#include "pica.h"
#include "shader_translator.h"
#include "video_core.h"
#include "renderer_opengl/renderer_opengl.h"

#include "nihstro/shader_bytecode.h"

#include "common/string_util.h"

const char g_glsl_shader_header[] = R"(#version 150

in vec4 v[16];

out vec4 o[16];

uniform vec4 c[96];
uniform bool b[16];
uniform int i[4];
uniform int aL;

vec4 r[16];
ivec2 idx;
bvec2 cmp;

)";

class IfElseData
{
public:
    IfElseData(u32 num_if_instr, u32 num_else_instr) : stage(2), num_if_instr(num_if_instr), num_else_instr(num_else_instr) {

    }

    u32 stage;
    u32 num_if_instr;
    u32 num_else_instr;
};

// State used when translating
std::vector<IfElseData> g_if_else_offset_list;
std::map<u32, std::string> g_fn_offset_map;
u32 g_cur_fn_entry;

u32 GetRegMaskLen(u32 v)
{
    u32 out = 0;

    if (v & 0xF) {
        if (v & (1 << 3)) {
            out++;
        }
        if (v & (1 << 2)) {
            out++;
        }
        if (v & (1 << 1)) {
            out++;
        }
        if (v & 1) {
            out++;
        }
    }

    return out;
}

std::string ParseComponentMask(u32 v, u32 comp_num) {
    std::string out;

    if (v & 0xF) {
        out += ".";

        if (v & (1 << 3) && (comp_num == 0 || comp_num == 1)) {
            out += "x";
        }
        if (v & (1 << 2) && (comp_num == 0 || comp_num == 2)) {
            out += "y";
        }
        if (v & (1 << 1) && (comp_num == 0 || comp_num == 3)) {
            out += "z";
        }
        if (v & 1 && (comp_num == 0 || comp_num == 4)) {
            out += "w";
        }
    }

    if (out.compare(".xyzw") == 0) {
        return std::string();
    }

    return out;
}

std::string ParseComponentSwizzle(u32 v, u32 srcidx, bool clamp_swizzle) {
    u32 maxLen = clamp_swizzle ? GetRegMaskLen(v) : 4;

    v = (v >> (5 + 9 * srcidx)) & 0xFF;

    std::string out(".");
    
    static const char comp[] = { 'x', 'y', 'z', 'w' };
    for (u32 i = 0; i < maxLen; ++i) {
        out += comp[(v >> ((3 - i) * 2)) & 0x3];
    }

    if (out.compare(".xyzw") == 0) {
        return std::string();
    }

    return out;
}

std::string RegTxtSrc(nihstro::Instruction instr, bool is_mad, bool is_inverted, const u32* swizzle_data, u32 srcidx, bool clamp_swizzle) {
    std::string reg_text;

    u32 swizzle_idx;
    if (is_mad) {
        swizzle_idx = instr.mad.operand_desc_id.Value();
    } else {
        swizzle_idx = instr.common.operand_desc_id.Value();
    }

    const nihstro::SwizzlePattern& swizzle = *(nihstro::SwizzlePattern*)&swizzle_data[swizzle_idx];
    bool is_negated = (srcidx == 0 && swizzle.negate_src1) ||
                        (srcidx == 1 && swizzle.negate_src2) ||
                        (srcidx == 2 && swizzle.negate_src3);

    u8 v;
    if (is_mad) {
        if (srcidx == 0) {
            v = instr.mad.src1.Value();
        } else if (srcidx == 1) {
            v = instr.mad.src2.Value();
        } else if (srcidx == 2) {
            v = instr.mad.src3.Value();
        } else {
            // Should never happen
            v = 0;
        }
    } else {
        if (srcidx == 0) {
            v = instr.common.GetSrc1(is_inverted);
        } else if (srcidx == 1) {
            v = instr.common.GetSrc2(is_inverted);
        } else {
            // Should never happen
            v = 0;
        }
    }

    std::string index_string;

    if (srcidx == 0) {
        if (instr.common.address_register_index == 1) {
            index_string = " + idx.x";
        } else if (instr.common.address_register_index == 2) {
            index_string = " + idx.y";
        } else {
            // Bad offset, just don't use it
            index_string = "";
        }
    } else {
        index_string = "";
    }

    if (v < 0x80) {
        if (v < 0x10) {
            reg_text += "v[";
            reg_text += std::to_string(v & 0xF);
            reg_text += index_string;
            reg_text += "]";
        } else if (v < 0x20) {
            reg_text += "r[";
            reg_text += std::to_string(v - 0x10);
            reg_text += index_string;
            reg_text += "]";
        } else if (v < 0x80) {
            reg_text += "c[";
            reg_text += std::to_string(v - 0x20);
            reg_text += index_string;
            reg_text += "]";
        } else {
            reg_text += "r[";
            reg_text += std::to_string(v);
            reg_text += index_string;
            reg_text += "]";
        }
    } else if (v < 0x88) {
        reg_text += "i[";
        reg_text += std::to_string(v - 0x80);
        reg_text += index_string;
        reg_text += "]";
    } else {
        reg_text += "b[";
        reg_text += std::to_string(v - 0x88);
        reg_text += index_string;
        reg_text += "]";
    }

    return (is_negated ? "-" : "") + reg_text + ParseComponentSwizzle(swizzle_data[swizzle_idx], srcidx, clamp_swizzle);
}

std::string RegTxtDst(u8 v, u32 mask, u32 comp_num) {
    std::string reg_text;

    if (v < 0x10) {
        reg_text += "o[";
        reg_text += std::to_string(v);
        reg_text += "]";
    } else if (v < 0x20) {
        reg_text += "r[";
        reg_text += std::to_string(v - 0x10);
        reg_text += "]";
    } else {
        reg_text += "r[";
        reg_text += std::to_string(v);
        reg_text += "]";
    }

    return reg_text + ParseComponentMask(mask, comp_num);
}

std::string PICAInstrToGLSL(nihstro::Instruction instr, const u32* swizzle_data) {
    std::string instr_text;

    nihstro::OpCode::Info info = instr.opcode.Value().GetInfo();

    if (info.type == nihstro::OpCode::Type::Arithmetic) {
        bool is_inverted = info.subtype & nihstro::OpCode::Info::SrcInversed;

        bool clamp_swizzle = instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::ADD ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::MUL ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::MAX ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::MIN ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::RCP ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::RSQ ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::MOV ||
            instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::MOVA;

        std::string dst = RegTxtDst(instr.common.dest.Value(), swizzle_data[instr.common.operand_desc_id.Value()], 0);
        std::string src1 = RegTxtSrc(instr, false, is_inverted, swizzle_data, 0, clamp_swizzle);
        std::string src2 = RegTxtSrc(instr, false, is_inverted, swizzle_data, 1, clamp_swizzle);

        switch (instr.opcode.Value().EffectiveOpCode())
        {
        case nihstro::OpCode::Id::ADD:
        {
            instr_text += dst;
            instr_text += " = ";
            instr_text += src1;
            instr_text += " + ";
            instr_text += src2;
            instr_text += ";\n";
            break;
        }

        case nihstro::OpCode::Id::DP3:
        case nihstro::OpCode::Id::DP4:
        {
            u32 mask_len = GetRegMaskLen(swizzle_data[instr.common.operand_desc_id.Value()]);
            if (mask_len == 1) {
                instr_text += dst;
            } else {
                for (u32 i = 1; i < mask_len; i++) {
                    instr_text += RegTxtDst(instr.common.dest.Value(), swizzle_data[instr.common.operand_desc_id.Value()], i);
                    instr_text += " = ";
                }

                instr_text += RegTxtDst(instr.common.dest.Value(), swizzle_data[instr.common.operand_desc_id.Value()], mask_len);
            }

            instr_text += " = dot(";
            instr_text += src1;
            instr_text += ", ";
            instr_text += src2;
            instr_text += ");\n";

            break;
        }

        case nihstro::OpCode::Id::MUL:
        {
            instr_text += dst;
            instr_text += " = ";
            instr_text += src1;
            instr_text += " * ";
            instr_text += src2;
            instr_text += ";\n";
            break;
        }

        case nihstro::OpCode::Id::MAX:
        {
            instr_text += dst;
            instr_text += " = max(";
            instr_text += src1;
            instr_text += ", ";
            instr_text += src2;
            instr_text += ");\n";
            break;
        }

        case nihstro::OpCode::Id::MIN:
        {
            instr_text += dst;
            instr_text += " = min(";
            instr_text += src1;
            instr_text += ", ";
            instr_text += src2;
            instr_text += ");\n";
            break;
        }

        case nihstro::OpCode::Id::RCP:
        {
            instr_text += "if (length(";
            instr_text += src1;
            instr_text += ") > 0.0000001) {";
            instr_text += dst;
            instr_text += " = 1 / ";
            instr_text += src1;
            instr_text += ";}\n";
            break;
        }

        case nihstro::OpCode::Id::RSQ:
        {
            instr_text += "if (length(";
            instr_text += src1;
            instr_text += ") > 0.0000001) {";
            instr_text += dst;
            instr_text += " = inversesqrt(";
            instr_text += src1;
            instr_text += ");}\n";
            break;
        }

        case nihstro::OpCode::Id::MOVA:
        {
            u32 maskSz = GetRegMaskLen(swizzle_data[instr.common.operand_desc_id.Value()]);
            if (maskSz == 2) {
                instr_text += "idx.xy = ivec2(";
                instr_text += src1;
                instr_text += ");\n";
            } else if (maskSz == 3) {
                instr_text += "idx.xyz = ivec3(";
                instr_text += src1;
                instr_text += ");\n";
            } else if (maskSz == 4) {
                instr_text += "idx.xyzw = ivec4(";
                instr_text += src1;
                instr_text += ");\n";
            } else {
                instr_text += "idx.x = int(";
                instr_text += src1;
                instr_text += ");\n";
            }

            break;
        }

        case nihstro::OpCode::Id::MOV:
        {
            instr_text += dst;
            instr_text += " = ";
            instr_text += src1;
            instr_text += ";\n";
            break;
        }

        case nihstro::OpCode::Id::CMP:
        {
            instr_text += "cmp.x = ";
            instr_text += src1;
            instr_text += ".x ";
            instr_text += instr.common.compare_op.ToString(instr.common.compare_op.x);
            instr_text += " ";
            instr_text += src2;
            instr_text += ".x; cmp.y = ";
            instr_text += src1;
            instr_text += ".y ";
            instr_text += instr.common.compare_op.ToString(instr.common.compare_op.y);
            instr_text += " ";
            instr_text += src2;
            instr_text += ".y;\n";
            break;
        }

        default:
        {
            instr_text = Common::StringFromFormat("// Unknown Arithmetic instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
        }
    } else if (info.type == nihstro::OpCode::Type::Conditional) {
        switch (instr.opcode.Value().EffectiveOpCode())
        {
        case nihstro::OpCode::Id::BREAKC:
        {
            instr_text = "break;\n";
            break;
        }

        case nihstro::OpCode::Id::CALL:
        {
            // Avoid recursive calls (occurs with multiple main()s)
            if (g_cur_fn_entry != instr.flow_control.dest_offset.Value()) {
                std::map<u32, std::string>::iterator call_offset = g_fn_offset_map.find(instr.flow_control.dest_offset.Value());
                if (call_offset != g_fn_offset_map.end()) {
                    instr_text += call_offset->second;
                    instr_text += "();\n";
                } else {
                    instr_text = "// CALL to unknown offset\n";
                }
            } else {
                instr_text = "// Culled recursive CALL\n";
            }

            break;
        }

        case nihstro::OpCode::Id::CALLC:
        {
            // Avoid recursive calls (occurs with multiple main()s)
            if (g_cur_fn_entry != instr.flow_control.dest_offset.Value()) {
                std::map<u32, std::string>::iterator call_offset = g_fn_offset_map.find(instr.flow_control.dest_offset.Value());
                if (call_offset != g_fn_offset_map.end()) {
                    char negate_or_space_x = instr.flow_control.refx.Value() ? ' ' : '!';
                    char negate_or_space_y = instr.flow_control.refy.Value() ? ' ' : '!';

                    switch (instr.flow_control.op)
                    {
                    case nihstro::Instruction::FlowControlType::Or:
                        instr_text += "if (";
                        instr_text += negate_or_space_x;
                        instr_text += "cmp.x || ";
                        instr_text += negate_or_space_y;
                        instr_text += "cmp.y) { ";
                        instr_text += call_offset->second;
                        instr_text += "(); }\n";
                        break;

                    case nihstro::Instruction::FlowControlType::And:
                        instr_text += "if (";
                        instr_text += negate_or_space_x;
                        instr_text += "cmp.x && ";
                        instr_text += negate_or_space_y;
                        instr_text += "cmp.y) { ";
                        instr_text += call_offset->second;
                        instr_text += "(); }\n";
                        break;

                    case nihstro::Instruction::FlowControlType::JustX:
                        instr_text += "if (";
                        instr_text += negate_or_space_x;
                        instr_text += "cmp.x) { ";
                        instr_text += call_offset->second;
                        instr_text += "(); }\n";
                        break;

                    case nihstro::Instruction::FlowControlType::JustY:
                        instr_text += "if (";
                        instr_text += negate_or_space_y;
                        instr_text += "cmp.y) { ";
                        instr_text += call_offset->second;
                        instr_text += "(); }\n";
                        break;

                    default:
                        instr_text = "// Bad CALLC condition op\n";
                        break;
                    }
                } else {
                    instr_text = "// CALLC to unknown offset\n";
                }
            } else {
                instr_text = "// Culled recursive CALLC\n";
            }

            break;
        }

        case nihstro::OpCode::Id::IFC:
        {
            char negate_or_space_x = instr.flow_control.refx.Value() ? ' ' : '!';
            char negate_or_space_y = instr.flow_control.refy.Value() ? ' ' : '!';

            switch (instr.flow_control.op)
            {
            case nihstro::Instruction::FlowControlType::Or:
                instr_text += "if (";
                instr_text += negate_or_space_x;
                instr_text += "cmp.x || ";
                instr_text += negate_or_space_y;
                instr_text += "cmp.y) {\n";
                instr_text = Common::StringFromFormat("if (%ccmp.x || %ccmp.y) {\n", negate_or_space_x, negate_or_space_y);
                break;

            case nihstro::Instruction::FlowControlType::And:
                instr_text += "if (";
                instr_text += negate_or_space_x;
                instr_text += "cmp.x && ";
                instr_text += negate_or_space_y;
                instr_text += "cmp.y) {\n";
                break;

            case nihstro::Instruction::FlowControlType::JustX:
                instr_text += "if (";
                instr_text += negate_or_space_x;
                instr_text += "cmp.x) {\n";
                break;

            case nihstro::Instruction::FlowControlType::JustY:
                instr_text += "if (";
                instr_text += negate_or_space_y;
                instr_text += "cmp.y) {\n";
                break;

            default:
                instr_text = "// Bad IFC condition op\n";
                break;
            }

            break;
        }

        case nihstro::OpCode::Id::JMPC:
        {
            // TODO: figure out how to function split with JMPs
            instr_text = "// JMPC not supported by GLSL\n";
            break;
        }

        case nihstro::OpCode::Id::JMPU:
        {
            instr_text = "// JMPU not supported by GLSL\n";
            break;
        }

        default:
        {
            instr_text = Common::StringFromFormat("// Unknown Conditional instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
        }
    } else if (info.type == nihstro::OpCode::Type::UniformFlowControl) {
        switch (instr.opcode.Value().EffectiveOpCode())
        {
        case nihstro::OpCode::Id::CALLU:
        {
            // Avoid recursive calls (occurs with multiple main()s)
            if (g_cur_fn_entry != instr.flow_control.dest_offset.Value()) {
                std::map<u32, std::string>::iterator call_offset = g_fn_offset_map.find(instr.flow_control.dest_offset.Value());
                if (call_offset != g_fn_offset_map.end()) {
                    instr_text += "if (b[";
                    instr_text += std::to_string(instr.flow_control.bool_uniform_id.Value());
                    instr_text += "]) { ";
                    instr_text += call_offset->second;
                    instr_text += "(); }\n";
                } else {
                    instr_text = "// CALLU to unknown offset\n";
                }
            } else {
                instr_text = "// Culled recursive CALLU\n";
            }

            break;
        }

        case nihstro::OpCode::Id::IFU:
        {
            instr_text += "if (b[";
            instr_text += std::to_string(instr.flow_control.bool_uniform_id.Value());
            instr_text += "]) {\n";
            break;
        }

        case nihstro::OpCode::Id::LOOP:
        {
            // TODO: implement this
            // make it push an IfElseData(offset, 0) and the end brace will automatically be added
            // hopefully that doesnt get out of order with an if+else though - because it might put a bracket after the else?
            instr_text = "// LOOP not yet implemented\n";
            break;
        }

        default:
        {
            instr_text = Common::StringFromFormat("// Unknown UniformFlowControl instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
        }
    } else if (info.type == nihstro::OpCode::Type::MultiplyAdd) {
        std::string dst = RegTxtDst(instr.mad.dest.Value(), swizzle_data[instr.mad.operand_desc_id.Value()], 0);
        std::string src1 = RegTxtSrc(instr, true, false, swizzle_data, 0, true);
        std::string src2 = RegTxtSrc(instr, true, false, swizzle_data, 1, true);
        std::string src3 = RegTxtSrc(instr, true, false, swizzle_data, 2, true);

        switch (instr.opcode.Value().EffectiveOpCode())
        {
        case nihstro::OpCode::Id::MADI:
            instr_text = "// MADI not yet implemented\n";
            break;

        case nihstro::OpCode::Id::MAD:
            instr_text += dst;
            instr_text += " = ";
            instr_text += src1;
            instr_text += " * ";
            instr_text += src2;
            instr_text += " + ";
            instr_text += src3;
            instr_text += ";\n";
            break;

        default:
            instr_text = Common::StringFromFormat("// Unknown MultiplyAdd instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
    } else if (info.type == nihstro::OpCode::Type::Trivial) {
        instr_text = "// Ignored trivial\n";
    } else if (info.type == nihstro::OpCode::Type::SetEmit) {
        instr_text = "// Unimplemented setemit\n";
    } else {
        instr_text = Common::StringFromFormat("// Unknown instruction 0x%08X\n", *((u32*)&instr));
    }

    return instr_text;
}

std::string PICABinToGLSL(u32 main_offset, const u32* shader_data, const u32* swizzle_data) {
    std::string glsl_shader(g_glsl_shader_header);

    int fn_counter = 0;
    int nest_depth = 0;
    bool main_opened = false;
    bool last_was_nop = false;

    g_if_else_offset_list.clear();
    g_fn_offset_map.clear();
    g_cur_fn_entry = 0;

    // First pass: scan for CALLs to determine function offsets
    for (int i = 0; i < 1024; ++i) {
        nihstro::Instruction instr = ((nihstro::Instruction*)shader_data)[i];

        if (instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::CALL || instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::CALLC || instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::CALLU) {
            if (g_fn_offset_map.find(instr.flow_control.dest_offset.Value()) == g_fn_offset_map.end()) {
                std::string fnName = std::string("fn") + std::to_string(g_fn_offset_map.size());
                g_fn_offset_map.emplace(instr.flow_control.dest_offset.Value(), fnName);

                glsl_shader += "void ";
                glsl_shader += fnName;
                glsl_shader += "();\n";
            }
        }
    }

    // Second pass: translate instructions
    for (int i = 0; i < 1024; ++i) {
        if (shader_data[i] == 0x0000000) {
            break;
        }

        // Consume ifelse offset if points to current offset
        for (IfElseData& ifelse_data : g_if_else_offset_list) {
            if (ifelse_data.stage == 2) {
                if (ifelse_data.num_if_instr == 1) {
                    nest_depth--;
                    glsl_shader += std::string(nest_depth, '\t') + "}\n";

                    if (ifelse_data.num_else_instr > 0) {
                        glsl_shader += std::string(nest_depth, '\t') + "else {\n";
                        nest_depth++;

                        ifelse_data.stage--;
                    } else {
                        ifelse_data.stage -= 2;
                    }
                } else {
                    ifelse_data.num_if_instr--;
                }
            } else if (ifelse_data.stage == 1) {
                if (ifelse_data.num_else_instr == 1) {
                    ifelse_data.stage--;

                    nest_depth--;
                    glsl_shader += std::string(nest_depth, '\t') + "}\n";
                } else {
                    ifelse_data.num_else_instr--;
                }
            }
        }

        // Consume function offset if points to current offset
        std::map<u32, std::string>::iterator fn_offset = g_fn_offset_map.find(i);
        if (fn_offset != g_fn_offset_map.end()) {
            if (nest_depth > 0) {
                nest_depth--;
                glsl_shader += std::string(nest_depth, '\t') + "}\n\n";
            }

            g_cur_fn_entry = i;

            glsl_shader += std::string(nest_depth, '\t') + "void " + fn_offset->second + "() {\n";
            nest_depth++;
        } else if (i == main_offset) {
            // Hit entry point - get out of nested block if we're in one
            while (nest_depth > 0) {
                nest_depth--;
                glsl_shader += std::string(nest_depth, '\t') + "}\n\n";
            }

            g_cur_fn_entry = i;

            glsl_shader += std::string(nest_depth, '\t') + "void main() {\n";
            nest_depth++;

            glsl_shader += std::string(nest_depth, '\t') + "r[0] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[1] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[2] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[3] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[4] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[5] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[6] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[7] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[8] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[9] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[10] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[11] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[12] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[13] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[14] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "r[15] = vec4(0.0, 0.0, 0.0, 0.0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "idx = ivec2(0, 0);\n";
            glsl_shader += std::string(nest_depth, '\t') + "cmp = bvec2(false, false);\n";

            main_opened = true;
        }

        // Translate instruction at current offset
        // All instructions have to be in main or a function, else we've overrun actual shader data
        if (nest_depth > 0) {
            nihstro::Instruction instr = ((nihstro::Instruction*)shader_data)[i];

            if (instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::END) {
                if (nest_depth > 0) {
                    glsl_shader += std::string(nest_depth, '\t') + "gl_Position = vec4(o[0].x, -o[0].y, -o[0].z, o[0].w);\n}// END\n";
                    nest_depth--;
                } else {
                    glsl_shader += "\n";
                }
            } else if (instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::NOP) {
                glsl_shader += std::string(nest_depth, '\t') + "// NOP\n";
            } else {
                glsl_shader += std::string(nest_depth, '\t') + PICAInstrToGLSL(instr, swizzle_data);

                if (instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::IFU || instr.opcode.Value().EffectiveOpCode() == nihstro::OpCode::Id::IFC) {
                    g_if_else_offset_list.emplace_back(instr.flow_control.dest_offset.Value() - i, instr.flow_control.num_instructions.Value());

                    nest_depth++;
                }
            }
        }
    }

    // Close function if it was last thing
    if (nest_depth > 0) {
        glsl_shader += "}\n";
    }

    return glsl_shader;
}
