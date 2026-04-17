#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "global.shared"
#include "vertex.shared"

layout(push_constant) uniform constants {
    mat4 model;
} push;

void main() {
    gl_Position = global.projection * global.view * push.model * vec4(in_position.xyz, 1.0f);
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(location = 0) out uint out_id;

layout(push_constant) uniform constants {
    mat4 model;
    uint node_id;
} push;

void main() {
    out_id = push.node_id;
}

#endif
