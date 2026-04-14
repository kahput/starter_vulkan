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

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = global.projection * global.view * push.model * vec4(in_position, 1.0f);
    uv = in_uv;
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform MaterialParameters {
    vec4 base_color_factor;
    vec3 emissive_factor;
	float metallic_factor;
	float roughness_factor;
} material;
layout(set = 1, binding = 1) uniform sampler2D u_base_color_texture;
layout(set = 1, binding = 2) uniform sampler2D u_metallic_roughness_texture;
layout(set = 1, binding = 3) uniform sampler2D u_normal_texture;
layout(set = 1, binding = 4) uniform sampler2D u_occlusion_texture;
layout(set = 1, binding = 5) uniform sampler2D u_emissive_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;
layout(location = 1) out uint out_id;

layout(push_constant) uniform constants {
    mat4 model;
    uint node_id;
} push;

void main() {
    out_color = texture(u_base_color_texture, in_uv) * material.base_color_factor; // * material.tint;
    out_id = push.node_id;
}

#endif
