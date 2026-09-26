#version 450 core
#pragma shader_stage(vertex)

#define INOUT out

// --- shared_start ---

layout(set = 0, binding = 0) uniform sampler2D textures[2];

INOUT Varying {
    layout(location = 0) vec2 uv;
} v;

// --- shared_end ---

// --- source_start ---

const vec2 corners[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
const uint indices[6] = { 0, 2, 3, 0, 3, 1 };

void main() { 
    vec2 coord = corners[indices[gl_VertexIndex]];
    gl_Position = vec4(coord, 0.0, 1.0);
    v.uv = (coord + 1) * 0.5;
}

// --- source_end ---