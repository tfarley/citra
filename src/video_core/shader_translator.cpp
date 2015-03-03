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

in vec4 v[8];

out vec4 o[7];

uniform mat4 viewMat, projMat;

uniform vec4 c[96];
uniform bool b[11];
uniform int i[4];

vec4 r[16];
ivec4 idx;

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
std::string g_cmp_strings[2];
std::vector<IfElseData> g_if_else_offset_stack;
std::map<u32, std::string> g_fn_offset_map;

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

std::string ParseComponentMask(u32 v) {
    std::string out;

    if (v & 0xF) {
        out += ".";

        if (v & (1 << 3)) {
            out += "x";
        }
        if (v & (1 << 2)) {
            out += "y";
        }
        if (v & (1 << 1)) {
            out += "z";
        }
        if (v & 1) {
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
    
    char comp[] = { 'x', 'y', 'z', 'w' };
    for (int i = 0; i < maxLen; ++i) {
        out += comp[(v >> ((3 - i) * 2)) & 0x3];
    }

    if (out.compare(".xyzw") == 0) {
        return std::string();
    }

    return out;
}

std::string RegTxtSrc(nihstro::Instruction instr, bool is_mad, bool is_inverted, const u32* swizzle_data, u32 srcidx, bool clamp_swizzle) {
    char reg_text[64];

    u32 swizzle_idx;
    if (is_mad) {
        swizzle_idx = instr.mad.operand_desc_id.Value();
    } else {
        swizzle_idx = instr.common.operand_desc_id.Value();
    }

    bool is_negated;
    const nihstro::SwizzlePattern& swizzle = *(nihstro::SwizzlePattern*)&swizzle_data[swizzle_idx];
    if (srcidx == 0 && swizzle.negate_src1) {
        is_negated = true;
    } else if (srcidx == 1 && swizzle.negate_src2) {
        is_negated = true;
    } else if (srcidx == 2 && swizzle.negate_src3) {
        is_negated = true;
    } else {
        is_negated = false;
    }

    u8 v;
    if (is_mad) {
        if (srcidx == 0) {
            v = instr.mad.src1.Value();
        } else if (srcidx == 1) {
            v = instr.mad.src2.Value();
        } else if (srcidx == 2) {
            v = instr.mad.src3.Value();
        }
    } else {
        if (srcidx == 0) {
            v = instr.common.GetSrc1(is_inverted);
        } else if (srcidx == 1) {
            v = instr.common.GetSrc2(is_inverted);
        }
    }

    const char* index_string = "";

    if (srcidx == 0) {
        if (instr.common.address_register_index == 1) {
            index_string = " + idx.x";
        } else if (instr.common.address_register_index == 2) {
            index_string = " + idx.y";
        } else {
            // Bad offset, just don't use it
        }
    }

    if (v < 0x80) {
        if (v < 0x10) {
            sprintf(reg_text, "v[%d%s]", v & 0xF, index_string);
        } else if (v < 0x20) {
            sprintf(reg_text, "r[%d%s]", v - 0x10, index_string);
        } else if (v < 0x80) {
            sprintf(reg_text, "c[%d%s]", v - 0x20, index_string);
        } else {
            sprintf(reg_text, "r[%d%s]", v, index_string);
        }
    } else if (v < 0x88) {
        sprintf(reg_text, "i[%d%s]", v - 0x80, index_string);
    } else {
        sprintf(reg_text, "b[%d%s]", v - 0x88, index_string);
    }

    return (is_negated ? "-" : "") + std::string(reg_text) + ParseComponentSwizzle(swizzle_data[swizzle_idx], srcidx, clamp_swizzle);
}

std::string RegTxtDst(u8 v, u32 mask) {
    char reg_text[32];

    if (v < 0x10) {
        sprintf(reg_text, "o[%d]", v);
    } else if (v < 0x20) {
        sprintf(reg_text, "r[%d]", v - 0x10);
    } else {
        sprintf(reg_text, "r[%d]", v);
    }

    return std::string(reg_text) + ParseComponentMask(mask);
}

std::string PICAInstrToGLSL(nihstro::Instruction instr, const u32* swizzle_data) {
    char instr_text[256];

    nihstro::Instruction::OpCodeInfo info = instr.opcode.GetInfo();

    if (info.type == nihstro::Instruction::OpCodeType::Arithmetic) {
        bool is_inverted = info.subtype & nihstro::Instruction::OpCodeInfo::SrcInversed;

        bool clamp_swizzle = instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::ADD ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::MUL ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::MAX ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::MIN ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::RCP ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::RSQ ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::MOV ||
            instr.opcode.EffectiveOpCode() == nihstro::Instruction::OpCode::MOVA;

        std::string dst = RegTxtDst(instr.common.dest.Value(), swizzle_data[instr.common.operand_desc_id.Value()]);
        std::string src1 = RegTxtSrc(instr, false, is_inverted, swizzle_data, 0, clamp_swizzle);
        std::string src2 = RegTxtSrc(instr, false, is_inverted, swizzle_data, 1, clamp_swizzle);

        switch (instr.opcode.EffectiveOpCode())
        {
        case nihstro::Instruction::OpCode::ADD:
        {
            sprintf(instr_text, "%s = %s + %s;\n", dst.c_str(), src1.c_str(), src2.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::DP3:
        {
            sprintf(instr_text, "%s = dot(%s, %s);\n", dst.c_str(), src1.c_str(), src2.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::DP4:
        {
            sprintf(instr_text, "%s = dot(%s, %s);\n", dst.c_str(), src1.c_str(), src2.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::MUL:
        {
            sprintf(instr_text, "%s = %s * %s;\n", dst.c_str(), src1.c_str(), src2.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::MAX:
        {
            sprintf(instr_text, "%s = max(%s, %s);\n", dst.c_str(), src1.c_str(), src2.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::MIN:
        {
            sprintf(instr_text, "%s = min(%s, %s);\n", dst.c_str(), src1.c_str(), src2.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::RCP:
        {
            sprintf(instr_text, "%s = 1 / %s;\n", dst.c_str(), src1.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::RSQ:
        {
            sprintf(instr_text, "%s = inversesqrt(%s);\n", dst.c_str(), src1.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::MOVA:
        {
            u32 maskSz = GetRegMaskLen(swizzle_data[instr.common.operand_desc_id.Value()]);
            if (maskSz == 2) {
                sprintf(instr_text, "idx.xy = ivec2(%s);\n", src1.c_str());
            } else if (maskSz == 3) {
                sprintf(instr_text, "idx.xyz = ivec3(%s);\n", src1.c_str());
            } else if (maskSz == 4) {
                sprintf(instr_text, "idx.xyzw = ivec4(%s);\n", src1.c_str());
            } else {
                sprintf(instr_text, "idx.x = int(%s);\n", src1.c_str());
            }

            break;
        }

        case nihstro::Instruction::OpCode::MOV:
        {
            sprintf(instr_text, "%s = %s;\n", dst.c_str(), src1.c_str());
            break;
        }

        case nihstro::Instruction::OpCode::CMP:
        {
            sprintf(instr_text, "%s.x %s %s.x", src1.c_str(), instr.common.compare_op.ToString(instr.common.compare_op.x).c_str(), src2.c_str());
            g_cmp_strings[0] = instr_text;

            sprintf(instr_text, "%s.y %s %s.y", src1.c_str(), instr.common.compare_op.ToString(instr.common.compare_op.y).c_str(), src2.c_str());
            g_cmp_strings[1] = instr_text;

            sprintf(instr_text, "// Culled CMP\n");

            break;
        }

        default:
        {
            sprintf(instr_text, "// Unknown Arithmetic instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
        }
    } else if (info.type == nihstro::Instruction::OpCodeType::Conditional) {
        switch (instr.opcode.EffectiveOpCode())
        {
        case nihstro::Instruction::OpCode::BREAKC:
        {
            sprintf(instr_text, "break;\n");
            break;
        }

        case nihstro::Instruction::OpCode::CALL:
        {
            std::map<u32, std::string>::iterator call_offset = g_fn_offset_map.find(instr.flow_control.dest_offset.Value());
            if (call_offset != g_fn_offset_map.end()) {
                sprintf(instr_text, "%s();\n", call_offset->second.c_str());
            } else {
                sprintf(instr_text, "// CALL to unknown offset\n");
            }
            break;
        }

        case nihstro::Instruction::OpCode::CALLC:
        {
            std::map<u32, std::string>::iterator call_offset = g_fn_offset_map.find(instr.flow_control.dest_offset.Value());
            if (call_offset != g_fn_offset_map.end()) {
                char negate_or_space_x = instr.flow_control.refx.Value() ? ' ' : '!';
                char negate_or_space_y = instr.flow_control.refy.Value() ? ' ' : '!';

                switch (instr.flow_control.op)
                {
                case nihstro::Instruction::FlowControlType::Or:
                    sprintf(instr_text, "if (%c(%s) || %c(%s)) { %s(); }\n", negate_or_space_x, g_cmp_strings[0].c_str(), negate_or_space_y, g_cmp_strings[1].c_str(), call_offset->second.c_str());
                    break;

                case nihstro::Instruction::FlowControlType::And:
                    sprintf(instr_text, "if (%c(%s) && %c(%s)) { %s(); }\n", negate_or_space_x, g_cmp_strings[0].c_str(), negate_or_space_y, g_cmp_strings[1].c_str(), call_offset->second.c_str());
                    break;

                case nihstro::Instruction::FlowControlType::JustX:
                    sprintf(instr_text, "if (%c(%s)) { %s(); }\n", negate_or_space_x, g_cmp_strings[0].c_str(), call_offset->second.c_str());
                    break;

                case nihstro::Instruction::FlowControlType::JustY:
                    sprintf(instr_text, "if (%c(%s)) { %s(); }\n", negate_or_space_y, g_cmp_strings[1].c_str(), call_offset->second.c_str());
                    break;

                default:
                    sprintf(instr_text, "// Bad CALLC condition op\n");
                    break;
                }
            } else {
                sprintf(instr_text, "// CALLC to unknown offset\n");
            }

            break;
        }

        case nihstro::Instruction::OpCode::IFC:
        {
            char negate_or_space_x = instr.flow_control.refx.Value() ? ' ' : '!';
            char negate_or_space_y = instr.flow_control.refy.Value() ? ' ' : '!';

            switch (instr.flow_control.op)
            {
            case nihstro::Instruction::FlowControlType::Or:
                sprintf(instr_text, "if (%c(%s) || %c(%s)) {\n", negate_or_space_x, g_cmp_strings[0].c_str(), negate_or_space_y, g_cmp_strings[1].c_str());
                break;

            case nihstro::Instruction::FlowControlType::And:
                sprintf(instr_text, "if (%c(%s) && %c(%s)) {\n", negate_or_space_x, g_cmp_strings[0].c_str(), negate_or_space_y, g_cmp_strings[1].c_str());
                break;

            case nihstro::Instruction::FlowControlType::JustX:
                sprintf(instr_text, "if (%c(%s)) {\n", negate_or_space_x, g_cmp_strings[0].c_str());
                break;

            case nihstro::Instruction::FlowControlType::JustY:
                sprintf(instr_text, "if (%c(%s)) {\n", negate_or_space_y, g_cmp_strings[1].c_str());
                break;

            default:
                sprintf(instr_text, "// Bad IFC condition op\n");
                break;
            }

            break;
        }

        case nihstro::Instruction::OpCode::JMPC:
        {
            // TODO: figure out how to function split with JMPs
            sprintf(instr_text, "// JMPC not supported by GLSL\n");
            break;
        }

        case nihstro::Instruction::OpCode::JMPU:
        {
            sprintf(instr_text, "// JMPU not supported by GLSL\n");
            break;
        }

        default:
        {
            sprintf(instr_text, "// Unknown Conditional instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
        }
    } else if (info.type == nihstro::Instruction::OpCodeType::UniformFlowControl) {
        switch (instr.opcode.EffectiveOpCode())
        {
        case nihstro::Instruction::OpCode::CALLU:
        {
            std::map<u32, std::string>::iterator call_offset = g_fn_offset_map.find(instr.flow_control.dest_offset.Value());
            if (call_offset != g_fn_offset_map.end()) {
                sprintf(instr_text, "if (b[%d]) { %s(); }\n", instr.flow_control.bool_uniform_id.Value(), call_offset->second.c_str());
            } else {
                sprintf(instr_text, "// CALLU to unknown offset\n");
            }

            break;
        }

        case nihstro::Instruction::OpCode::IFU:
        {
            sprintf(instr_text, "if (b[%d]) {\n", instr.flow_control.bool_uniform_id.Value());
            break;
        }

        case nihstro::Instruction::OpCode::LOOP:
        {
            // TODO: implement this
            // make it push an IfElseData(offset, 0) and the end brace will automatically be added
            // hopefully that doesnt get out of order with an if+else though - because it might put a bracket after the else?
            sprintf(instr_text, "// LOOP not yet implemented\n");
            break;
        }

        default:
        {
            sprintf(instr_text, "// Unknown UniformFlowControl instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
        }
    } else if (info.type == nihstro::Instruction::OpCodeType::MultiplyAdd) {
        std::string dst = RegTxtDst(instr.mad.dest.Value(), swizzle_data[instr.mad.operand_desc_id.Value()]);
        std::string src1 = RegTxtSrc(instr, true, false, swizzle_data, 0, true);
        std::string src2 = RegTxtSrc(instr, true, false, swizzle_data, 1, true);
        std::string src3 = RegTxtSrc(instr, true, false, swizzle_data, 2, true);

        switch (instr.opcode.EffectiveOpCode())
        {
        case nihstro::Instruction::OpCode::MADI:
            sprintf(instr_text, "// MADI not yet implemented\n");
            break;

        case nihstro::Instruction::OpCode::MAD:
            sprintf(instr_text, "%s = %s * %s + %s;\n", dst.c_str(), src1.c_str(), src2.c_str(), src3.c_str());
            break;

        default:
            sprintf(instr_text, "// Unknown MultiplyAdd instruction 0x%08X\n", *((u32*)&instr));
            break;
        }
    } else if (info.type == nihstro::Instruction::OpCodeType::Trivial) {
        sprintf(instr_text, "// Ignored trivial\n");
    } else if (info.type == nihstro::Instruction::OpCodeType::SetEmit) {
        sprintf(instr_text, "// Unimplemented setemit\n");
    } else {
        sprintf(instr_text, "// Unknown instruction 0x%08X\n", *((u32*)&instr));
    }

    return std::string(instr_text);
}

std::string PICABinToGLSL(const u32* shader_data, const u32* swizzle_data) {
    std::string glsl_shader(g_glsl_shader_header);

    int fn_counter = 0;
    int nest_depth = 0;
    bool main_opened = false;
    bool main_closed = false;
    u32 main_end_offset = 0;
    bool last_was_nop = false;

    g_if_else_offset_stack.clear();
    g_fn_offset_map.clear();

    // First pass: scan for CALLs to determine function offsets
    for (int i = 0; i < 1024; ++i) {
        nihstro::Instruction instr = ((nihstro::Instruction*)shader_data)[i];

        if (instr.opcode == nihstro::Instruction::OpCode::CALL || instr.opcode == nihstro::Instruction::OpCode::CALLC || instr.opcode == nihstro::Instruction::OpCode::CALLU) {
            if (g_fn_offset_map.find(instr.flow_control.dest_offset.Value()) == g_fn_offset_map.end()) {
                char fnName[32];
                sprintf(fnName, "fn%d", g_fn_offset_map.size());
                g_fn_offset_map.insert(std::pair<u32, std::string>(instr.flow_control.dest_offset.Value(), fnName));

                glsl_shader += "void " + std::string(fnName) + "();\n";
            }
        }
    }

    // Second pass: translate instructions
    for (int i = 0; i < 1024; ++i) {
        if (shader_data[i] == 0x0000000) {
            break;
        }

        // Consume ifelse offset if points to current offset
        for (std::vector<IfElseData>::iterator cur_ifelse_it = g_if_else_offset_stack.begin(); cur_ifelse_it != g_if_else_offset_stack.end(); ++cur_ifelse_it) {
            if (cur_ifelse_it->stage == 2) {
                if (cur_ifelse_it->num_if_instr == 1) {
                    nest_depth--;
                    glsl_shader += std::string(nest_depth, '\t') + "}\n";

                    if (cur_ifelse_it->num_else_instr > 0) {
                        glsl_shader += std::string(nest_depth, '\t') + "else {\n";
                        nest_depth++;

                        cur_ifelse_it->stage--;
                    } else {
                        cur_ifelse_it->stage -= 2;
                    }
                } else {
                    cur_ifelse_it->num_if_instr--;
                }
            } else if (cur_ifelse_it->stage == 1) {
                if (cur_ifelse_it->num_else_instr == 1) {
                    cur_ifelse_it->stage--;

                    nest_depth--;
                    glsl_shader += std::string(nest_depth, '\t') + "}\n";
                } else {
                    cur_ifelse_it->num_else_instr--;
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

            glsl_shader += std::string(nest_depth, '\t') + "void " + fn_offset->second + "() {\n";
            nest_depth++;
        }
        else if (nest_depth == 0 && !main_opened) {
            glsl_shader += std::string(nest_depth, '\t') + "void main() {\n";
            nest_depth++;

            main_opened = true;
        }

        // Translate instruction at current offset
        nihstro::Instruction instr = ((nihstro::Instruction*)shader_data)[i];

        if (instr.opcode == nihstro::Instruction::OpCode::END) {
            if (nest_depth > 0) {
                main_closed = true;
                glsl_shader += std::string(nest_depth, '\t') + "gl_Position = vec4(-o[0].y, o[0].x, -o[0].z, o[0].w);\n}// END\n";
                nest_depth--;
            } else {
                glsl_shader += "\n";
            }
        } else if (instr.opcode == nihstro::Instruction::OpCode::NOP) {
            if (!main_opened) {
                //HACK: treat double-nop as end of function for cave story (since functions are ABOVE main())
                if (last_was_nop) {
                    if (nest_depth > 0) {
                        nest_depth--;
                        glsl_shader += std::string(nest_depth, '\t') + "}\n\n";
                    }

                    last_was_nop = false;
                } else {
                    glsl_shader += std::string(nest_depth, '\t') + "// NOP\n";
                    last_was_nop = true;
                }
            } else {
                glsl_shader += std::string(nest_depth, '\t') + "// NOP\n";
            }
        } else {
            glsl_shader += std::string(nest_depth, '\t') + PICAInstrToGLSL(instr, swizzle_data);

            if (instr.opcode == nihstro::Instruction::OpCode::IFU || instr.opcode == nihstro::Instruction::OpCode::IFC) {
                g_if_else_offset_stack.push_back(IfElseData(instr.flow_control.dest_offset.Value() - i, instr.flow_control.num_instructions.Value()));

                nest_depth++;
            }
        }

        if (main_opened && !main_closed) {
            main_end_offset = glsl_shader.length();
        }
    }

    // Close function if it was last thing
    if (nest_depth > 0) {
        glsl_shader += "}\n";
    }

    if (!main_closed) {
        glsl_shader.insert(main_end_offset, "\tgl_Position = vec4(-o[0].y, o[0].x, -o[0].z, o[0].w);\n");
    }

    return glsl_shader;
}
