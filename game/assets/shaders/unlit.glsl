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

layout(location = 0) out vec2 out_uv;

void main() {
    gl_Position = global.projection * global.view * push.model * vec4(batch.vertex_data[gl_VertexIndex].xy, 0.0f, 1.0f);
    out_uv = batch.vertex_data[gl_VertexIndex].zw;
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

#include "global.shared"

layout(set = 1, binding = 1) uniform sampler2D u_texture;
/* layout(set = 1, binding = 0) uniform MaterialParameters { */
/*     vec4 tint; */
/* } material; */


layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(u_texture, in_uv);
    if (color.a < 1.0) discard;

    out_color = color; // * material.tint;
}

#endif
