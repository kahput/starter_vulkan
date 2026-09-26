#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"  

struct LineInstance3D {
    vec4 a, b; // xyz + thickness
    uint color;
};

layout(set = 1, binding = 0) readonly buffer LineBlock {
    LineInstance3D instances[];
};

INOUT Varying {
    layout (location = 0) vec4 color;
} v;

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 out_color;

void main() {
    out_color = v.color; 
}

// --- source_end ---