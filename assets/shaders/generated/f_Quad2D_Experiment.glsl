#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"
#extension GL_EXT_nonuniform_qualifier : enable

struct Quad2D {
    vec2 position, size;
    vec4 radii;
    vec2 uvs[4];

    uint imageid, flags;
    uint fill_color, border_color;

    vec2 origin, rotation;
    float border_width;
    // vec3 _pad0;
};
layout(set = 0, binding = 1) readonly buffer InstanceBlock {
    Quad2D instances[];
};
layout(set = 1, binding = 0) uniform sampler2D u_textures[32];

INOUT Varying {
    layout(location = 0) vec2 tex_coords;
    layout(location = 1) flat uint texture_id;

    layout(location = 2) vec4 fill_color;
    layout(location = 3) vec4 border_color;
    layout(location = 4) float border_width;

    layout(location = 5) vec2 size; 
    layout(location = 6) vec2 local; 
    flat layout(location = 7) vec4 radii;
} v;

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 out_color;

void main() {
    vec4 sampled = texture(u_textures[v.texture_id], v.tex_coords);

    float t = sin(frame.time * 8.0);

    float dist = distance(v.tex_coords, vec2(0.5));

    out_color = step(dist, mix(0.2, 1.0, t*0.5+0.5)) * sampled;
}

// --- source_end ---