#version 450 core
#pragma shader_stage(vertex)

#define INOUT out

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
    Vertex3 vertex_buffer[];
};
layout(set = 1, binding = 1) uniform sampler2D u_textures[TEXTURE_COUNT];
layout(set = 1, binding = 2) readonly buffer InstanceBlock {
    mat4 instances[];
};
layout(set = 1, binding = 3) uniform sampler2D u_heightmap;

layout(push_constant) uniform ConstantBlock {
    vec4 tint;
    vec4 emissive;
    vec2 metallic_roughness;
    vec4 uv_st;
    uint use_heightmap;
} pc;

INOUT Varying {
    layout (location = 0) vec3 worldspace;
    layout (location = 1) vec3 normal;
    layout (location = 2) vec2 uv;
    layout (location = 3) vec4 lightspace;
    layout (location = 4) float fog_strength;
} v;

// --- shared_end ---

// --- source_start ---

void main() {
    Vertex3 vertex = vertex_buffer[gl_VertexIndex];
    mat4 transform = instances[gl_InstanceIndex];

    v.worldspace = vec3(transform * vec4(vertex.position.xyz, 1.0));
    if (pc.use_heightmap == 1)
        v.worldspace.y += (texture(u_heightmap, (v.worldspace.xz / 256.0) + 0.5).r - 0.5) * 40;
    v.lightspace = lights[0].matrix * vec4(v.worldspace, 1.0f);

    v.normal = vec3(0.0, 1.0, 0.0);
    v.uv = transform_uv(vertex.uv, pc.uv_st);

    float dist = length(vec3(frame.view * vec4(v.worldspace, 1.0)));
    v.fog_strength = exp(-pow(dist * frame.fog_density, frame.fog_gradient));
    v.fog_strength = clamp(v.fog_strength, 0.0, 1.0);

    gl_Position = frame.projection * frame.view * vec4(v.worldspace, 1.0);
}

// --- source_end ---