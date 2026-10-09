#version 450 core
#pragma shader_stage(vertex)

#define INOUT out

// --- shared_start ---

#include "lib/frame.glsl"
struct Vertex {
    vec3 position;
    vec3 normals;
    vec2 uv0, uv1;
};
layout(set = 1, binding = 0) readonly buffer VertexBlock {
    Vertex vertex_buffer[];
};

INOUT layout(location = 0) Varying {
    vec2 uv;
};

// --- shared_end ---

// --- source_start ---

void main(void) {
    Vertex vertex = vertex_buffer[gl_VertexIndex];

    gl_Position = frame.clip_from_view * frame.view_from_world * vec4(vertex.position, 1.0);
    uv = vertex.uv0;
}

// --- source_end ---