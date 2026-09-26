#version 450 core
#pragma shader_stage(vertex)

#define INOUT out

// --- shared_start ---

#include "lib/frame.glsl"

layout(set = 1, binding = 0) readonly buffer VertexBlock {
    Vertex3 vertex_buffer[];
};

layout(push_constant) uniform ConstantBlock {
    mat4 model;
} pc;

// --- shared_end ---

// --- source_start ---

void main() {
    Vertex3 vertex = vertex_buffer[gl_VertexIndex];
    gl_Position = frame.view * pc.model * vec4(vertex.position.xyz, 1.0);
}

// --- source_end ---