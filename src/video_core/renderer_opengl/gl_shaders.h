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

in vec4 vert_position;
in vec4 vert_color;
in vec2 vert_texcoords[3];

out vec4 o[7];

uniform int out_maps[7*4];

void SetVal(int map_idx, float val) {
    o[out_maps[map_idx] / 4][out_maps[map_idx] % 4] = val;
}

void main() {
    SetVal(8, vert_color.x);
    SetVal(9, vert_color.y);
    SetVal(10, vert_color.z);
    SetVal(11, vert_color.w);
    SetVal(12, vert_texcoords[0].x);
    SetVal(13, vert_texcoords[0].y);

    // TODO: These are wrong/broken
    SetVal(14, vert_texcoords[1].x);
    SetVal(15, vert_texcoords[1].y);
    SetVal(16, vert_texcoords[2].x);
    SetVal(17, vert_texcoords[2].y);

    gl_Position = vec4(vert_position.x, -vert_position.y, -vert_position.z, vert_position.w);
}
)";

const char g_fragment_shader_hw[] = R"(
#version 150 core

in vec4 o[7];
out vec4 color;

uniform int alphatest_func;
uniform float alphatest_ref;

uniform sampler2D tex[3];

struct TEVConfig
{
    ivec3 color_sources;
    ivec3 alpha_sources;
    ivec3 color_modifiers;
    ivec3 alpha_modifiers;
    ivec2 color_alpha_op;
    vec2 color_alpha_multiplier;
    vec4 const_color;
    bvec2 updates_combiner_buffer_color_alpha;
};

uniform TEVConfig tev_cfgs[6];

uniform int out_maps[7*4];

vec4 g_combiner_buffer;
vec4 g_last_tex_env_out;
vec4 g_const_color;

float GetVal(int map_idx) {
    return o[out_maps[map_idx] / 4][out_maps[map_idx] % 4];
}

vec4 GetSource(int source) {
    if (source == 0) {
        // HACK: Should use values 8/9/10/11 but hurts framerate
        return o[out_maps[8] >> 2];
    }
    else if (source == 1) {
        return o[out_maps[8] >> 2];
    }
    else if (source == 3) {
        return texture(tex[0], vec2(GetVal(12), GetVal(13)));
    }
    else if (source == 4) {
        return texture(tex[1], vec2(GetVal(14), GetVal(15)));
    }
    else if (source == 5) {
        // TODO: Unverified
        return texture(tex[2], vec2(GetVal(16), GetVal(17)));
    }
    else if (source == 6) {
        // TODO: no 4th texture?
    }
    else if (source == 13) {
        return g_combiner_buffer;
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
        return color[0] + color[1] - vec3(0.5, 0.5, 0.5);
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
        return alpha[0] + alpha[1] - 0.5;
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
    g_const_color = tev_cfgs[tex_env_idx].const_color;

    vec3 color_results[3] = vec3[3](GetColorModifier(tev_cfgs[tex_env_idx].color_modifiers.x, GetSource(tev_cfgs[tex_env_idx].color_sources.x)),
                                    GetColorModifier(tev_cfgs[tex_env_idx].color_modifiers.y, GetSource(tev_cfgs[tex_env_idx].color_sources.y)),
                                    GetColorModifier(tev_cfgs[tex_env_idx].color_modifiers.z, GetSource(tev_cfgs[tex_env_idx].color_sources.z)));
    vec3 color_output = ColorCombine(tev_cfgs[tex_env_idx].color_alpha_op.x, color_results);

    float alpha_results[3] = float[3](GetAlphaModifier(tev_cfgs[tex_env_idx].alpha_modifiers.x, GetSource(tev_cfgs[tex_env_idx].alpha_sources.x)),
                                      GetAlphaModifier(tev_cfgs[tex_env_idx].alpha_modifiers.y, GetSource(tev_cfgs[tex_env_idx].alpha_sources.y)),
                                      GetAlphaModifier(tev_cfgs[tex_env_idx].alpha_modifiers.z, GetSource(tev_cfgs[tex_env_idx].alpha_sources.z)));
    float alpha_output = AlphaCombine(tev_cfgs[tex_env_idx].color_alpha_op.y, alpha_results);

    g_last_tex_env_out = vec4(min(color_output * tev_cfgs[tex_env_idx].color_alpha_multiplier.x, 1.0), min(alpha_output * tev_cfgs[tex_env_idx].color_alpha_multiplier.y, 1.0));

    if (tev_cfgs[tex_env_idx].updates_combiner_buffer_color_alpha.x) {
        g_combiner_buffer.rgb = g_last_tex_env_out.rgb;
    }

    if (tev_cfgs[tex_env_idx].updates_combiner_buffer_color_alpha.y) {
        g_combiner_buffer.a = g_last_tex_env_out.a;
    }
}

void main(void) {
        ProcessTexEnv(0);

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
