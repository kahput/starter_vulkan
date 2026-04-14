#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "global.shared"

layout(push_constant) uniform constants {
    mat4 model;
} push;

layout(location = 0) flat out vec4 out_color;

void main() {
    gl_Position = global.projection * global.view * push.model * vec4(in_position, 1.0f);
    out_color = in_color;
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(location = 0) flat in vec4 in_color;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = in_color; // * material.tint;
}

#endif
