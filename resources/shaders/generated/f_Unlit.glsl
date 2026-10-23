#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"
struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv0, uv1;
};
layout(set = 1, binding = 0) readonly buffer VertexBlock {
    Vertex vertex_buffer[];
};

layout(push_constant) uniform ConstantBlock {
    mat4 world_from_local;
};

INOUT layout(location = 0) Varying {
    vec3 normal;
    vec2 uv;
};

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 color;

void main(void) {
    color = vec4(1.0);
}

// --- source_end ---