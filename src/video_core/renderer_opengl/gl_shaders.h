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

void ProcessTexEnv(int tex_env_idx) {
    // x = int sources;
    // y = int modifiers;
    // z = int ops;
    // w = int const_col;

    // TODO: make this actually do tex env stuff

    g_last_tex_env_out = o[out_maps[2]] * texture(tex[0], o[out_maps[3]].xy);
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
