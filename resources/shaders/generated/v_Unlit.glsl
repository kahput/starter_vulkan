#version 450 core
#pragma shader_stage(vertex)

#define INOUT out

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

void main(void) {
    Vertex vertex = vertex_buffer[gl_VertexIndex];

    gl_Position = frame.clip_from_view * frame.view_from_world * world_from_local * vec4(vertex.position, 1.0);
    uv = vertex.uv0;
    normal = vertex.normal;
}

// --- source_end ---