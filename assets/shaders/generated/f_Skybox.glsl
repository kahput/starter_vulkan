#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"

INOUT Varying {
    layout (location = 0) vec3 worldspace;
} v;

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_skybox, v.worldspace); 
}

// --- source_end ---