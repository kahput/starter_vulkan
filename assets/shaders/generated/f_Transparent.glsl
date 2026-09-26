#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"
#include "lib/shadow.glsl"

#define TEXTURE_ALBEDO 0
#define TEXTURE_METAL_ROUGHNESS 1
#define TEXTURE_NORMAL 2
#define TEXTURE_OCCLUSION 3
#define TEXTURE_EMISSIVE 4
#define TEXTURE_COUNT 5

layout(set = 1, binding = 0) readonly buffer VertexBlock {
    Vertex3 vertices[];
} draw;
layout(set = 1, binding = 1) uniform sampler2D u_textures[TEXTURE_COUNT];

layout(push_constant) uniform ConstantBlock {
    mat4 model;
    vec4 tint;
    vec4 emissive;
    vec2 metallic_roughness;
    vec4 uv_st;
} pc;

INOUT Varying { // INOUT inside vertex = out, INOUT inside fragment = in
    layout (location = 0) vec3 worldspace;
    layout (location = 1) vec3 normal;
    layout (location = 2) vec2 uv;
    layout (location = 3) vec4 lightspace;
    layout (location = 4) float fog_strength;
} v;

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 out_color;

void main() {
    if (abs(dot(v.normal, vec3(0.0, 1.0, 0.0))) > 0.95) discard;

    float t = abs(cos( (v.uv.y + frame.time * 0.2) * TAU * 2.0));

    /* out_color =  vec4(vec3(t), smoothstep(0.00, 0.5, t) * pow(1.0 - v.uv.y, 3.0)); */
    out_color = vec4(vec3(smoothstep(0.00, 0.5, t)), 1.0);
}

// --- source_end ---