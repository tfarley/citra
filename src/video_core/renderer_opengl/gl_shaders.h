// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

namespace GLShaders {

const char g_vertex_shader[] = R"(
#version 150 core

in vec2 vert_position;
in vec2 vert_tex_coord;
out vec2 frag_tex_coord;

// This is a truncated 3x3 matrix for 2D transformations:
// The upper-left 2x2 submatrix performs scaling/rotation/mirroring.
// The third column performs translation.
// The third row could be used for projection, which we don't need in 2D. It hence is assumed to
// implicitly be [0, 0, 1]
uniform mat3x2 modelview_matrix;

void main() {
    // Multiply input position by the rotscale part of the matrix and then manually translate by
    // the last column. This is equivalent to using a full 3x3 matrix and expanding the vector
    // to `vec3(vert_position.xy, 1.0)`
    gl_Position = vec4(mat2(modelview_matrix) * vert_position + modelview_matrix[2], 0.0, 1.0);
    frag_tex_coord = vert_tex_coord;
}
)";

const char g_fragment_shader[] = R"(
#version 150 core

in vec2 frag_tex_coord;
out vec4 color;

uniform sampler2D color_texture;

void main() {
    color = texture(color_texture, frag_tex_coord);
}
)";

const char g_vertex_shader_hw[] = R"(
#version 150 core

in vec4 v[8];
out vec4 o[7];

uniform int out_maps[7];

void main() {
    o[out_maps[2]] = v[1];
    o[out_maps[3]] = v[2];
    gl_Position = v[0];
}
)";

const char g_fragment_shader_hw[] = R"(
#version 150 core

in vec4 o[7];
out vec4 color;

uniform int alphatest_func;
uniform float alphatest_ref;

uniform sampler2D tex[3];
uniform ivec4 tevs[6];
uniform int out_maps[7];

vec4 g_last_tex_env_out;
vec4 g_const_color;

vec4 GetSource(int source) {
    if (source == 0) {
        return o[out_maps[2]];
    }
    else if (source == 1) {
        return o[out_maps[2]];
    }
    else if (source == 3) {
        return texture(tex[0], o[out_maps[3]].xy);
    }
    else if (source == 4) {
        return texture(tex[1], o[out_maps[3]].xy);
    }
    else if (source == 5) {
        return texture(tex[2], o[out_maps[3]].xy);
    }
    else if (source == 6) {
        // TODO: no 4th texture?
    }
    else if (source == 14) {
        return g_const_color;
    }
    else if (source == 15) {
        return g_last_tex_env_out;
    }

    return vec4(0.0, 0.0, 0.0, 0.0);
}

vec3 GetColorModifier(int factor, vec4 color) {
    if (factor == 0) {
        return color.rgb;
    }
    else if (factor == 1) {
        return vec3(1.0, 1.0, 1.0) - color.rgb;
    }
    else if (factor == 2) {
        return color.aaa;
    }
    else if (factor == 3) {
        return vec3(1.0, 1.0, 1.0) - color.aaa;
    }
    else if (factor == 4) {
        return color.rrr;
    }
    else if (factor == 5) {
        return vec3(1.0, 1.0, 1.0) - color.rrr;
    }
    else if (factor == 8) {
        return color.ggg;
    }
    else if (factor == 9) {
        return vec3(1.0, 1.0, 1.0) - color.ggg;
    }
    else if (factor == 12) {
        return color.bbb;
    }
    else if (factor == 13) {
        return vec3(1.0, 1.0, 1.0) - color.bbb;
    }

    return vec3(0.0, 0.0, 0.0);
}

float GetAlphaModifier(int factor, vec4 color) {
    if (factor == 0) {
        return color.a;
    }
    else if (factor == 1) {
        return 1.0 - color.a;
    }
    else if (factor == 2) {
        return color.r;
    }
    else if (factor == 3) {
        return 1.0 - color.r;
    }
    else if (factor == 4) {
        return color.g;
    }
    else if (factor == 5) {
        return 1.0 - color.g;
    }
    else if (factor == 6) {
        return color.b;
    }
    else if (factor == 7) {
        return 1.0 - color.b;
    }

    return 0.0;
}

vec3 ColorCombine(int op, vec3 color[3]) {
    if (op == 0) {
        return color[0];
    }
    else if (op == 1) {
        return color[0] * color[1];
    }
    else if (op == 2) {
        return min(color[0] + color[1], 1.0);
    }
    else if (op == 3) {
        // TODO: implement add signed
    }
    else if (op == 4) {
        return color[0] * color[2] + color[1] * (vec3(1.0, 1.0, 1.0) - color[2]);
    }
    else if (op == 5) {
        return max(color[0] - color[1], 0.0);
    }
    else if (op == 8) {
        return min(color[0] * color[1] + color[2], 1.0);
    }
    else if (op == 9) {
        return min(color[0] + color[1], 1.0) * color[2];
    }

    return vec3(0.0, 0.0, 0.0);
}

float AlphaCombine(int op, float alpha[3]) {
    if (op == 0) {
        return alpha[0];
    }
    else if (op == 1) {
        return alpha[0] * alpha[1];
    }
    else if (op == 2) {
        return min(alpha[0] + alpha[1], 1.0);
    }
    else if (op == 3) {
        // TODO: implement add signed
    }
    else if (op == 4) {
        return alpha[0] * alpha[2] + alpha[1] * (1.0 - alpha[2]);
    }
    else if (op == 5) {
        return max(alpha[0] - alpha[1], 0.0);
    }
    else if (op == 8) {
        return min(alpha[0] * alpha[1] + alpha[2], 1.0);
    }
    else if (op == 9) {
        return min(alpha[0] + alpha[1], 1.0) * alpha[2];
    }

    return 0.0;
}

void ProcessTexEnv(int tex_env_idx) {
    // x = int sources;
    // y = int modifiers;
    // z = int ops;
    // w = int const_col;

    int color_source1 = tevs[tex_env_idx].x & 0xF;
    int color_source2 = (tevs[tex_env_idx].x >> 4) & 0xF;
    int color_source3 = (tevs[tex_env_idx].x >> 8) & 0xF;
    int alpha_source1 = (tevs[tex_env_idx].x >> 16) & 0xF;
    int alpha_source2 = (tevs[tex_env_idx].x >> 20) & 0xF;
    int alpha_source3 = (tevs[tex_env_idx].x >> 24) & 0xF;

    int color_modifier1 = tevs[tex_env_idx].y & 0xF;
    int color_modifier2 = (tevs[tex_env_idx].y >> 4) & 0xF;
    int color_modifier3 = (tevs[tex_env_idx].y >> 8) & 0xF;
    int alpha_modifier1 = (tevs[tex_env_idx].y >> 12) & 0xF;
    int alpha_modifier2 = (tevs[tex_env_idx].y >> 16) & 0xF;
    int alpha_modifier3 = (tevs[tex_env_idx].y >> 20) & 0xF;

    int color_op = tevs[tex_env_idx].z & 0xF;
    int alpha_op = (tevs[tex_env_idx].z >> 16) & 0xF;

    float const_r = (tevs[tex_env_idx].w & 0xFF) / 255.0;
    float const_g = ((tevs[tex_env_idx].w >> 8) & 0xFF) / 255.0;
    float const_b = ((tevs[tex_env_idx].w >> 16) & 0xFF) / 255.0;
    float const_a = ((tevs[tex_env_idx].w >> 24) & 0xFF) / 255.0;

    g_const_color = vec4(const_r, const_g, const_b, const_a);

    vec3 color_results[3] = vec3[3](GetColorModifier(color_modifier1, GetSource(color_source1)),
                                    GetColorModifier(color_modifier2, GetSource(color_source2)),
                                    GetColorModifier(color_modifier3, GetSource(color_source3)));
    vec3 color_output = ColorCombine(color_op, color_results);

    float alpha_results[3] = float[3](GetAlphaModifier(alpha_modifier1, GetSource(alpha_source1)),
                                      GetAlphaModifier(alpha_modifier2, GetSource(alpha_source2)),
                                      GetAlphaModifier(alpha_modifier3, GetSource(alpha_source3)));
    float alpha_output = AlphaCombine(alpha_op, alpha_results);

    g_last_tex_env_out = vec4(color_output, alpha_output);
}

void main(void) {
    for (int i = 0; i < 6; ++i) {
        ProcessTexEnv(i);
    }

    if (alphatest_func == 0) {
        discard;
    }
    else if (alphatest_func == 2) {
        if (g_last_tex_env_out.a != alphatest_ref) {
            discard;
        }
    }
    else if (alphatest_func == 3) {
        if (g_last_tex_env_out.a == alphatest_ref) {
            discard;
        }
    }
    else if (alphatest_func == 4) {
        if (g_last_tex_env_out.a > alphatest_ref) {
            discard;
        }
    }
    else if (alphatest_func == 5) {
        if (g_last_tex_env_out.a >= alphatest_ref) {
            discard;
        }
    }
    else if (alphatest_func == 6) {
        if (g_last_tex_env_out.a < alphatest_ref) {
            discard;
        }
    }
    else if (alphatest_func == 7) {
        if (g_last_tex_env_out.a <= alphatest_ref) {
            discard;
        }
    }

    color = g_last_tex_env_out;
}
)";

}
