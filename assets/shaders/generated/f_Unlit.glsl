#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

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

    layout(location = 0) out vec4 color;

    void main(void) {
        color = vec4(vec3(uv.xxx), 1.0);
}

// --- source_end ---