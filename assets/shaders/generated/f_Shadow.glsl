#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

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
}

// --- source_end ---