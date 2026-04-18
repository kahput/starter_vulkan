#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "vertex.shared"

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = vec4(in_position.xyz, 1.0f);
    uv = in_uv;
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 0, binding = 0) uniform sampler2D u_screen;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_screen, vec2(in_uv.x, -in_uv.y));
}

#endif
