#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"
#extension GL_EXT_nonuniform_qualifier : enable

struct QuadInstance2D {
    vec2 position, size;
    vec4 radii;
    vec2 uvs[4];

    uint imageid, flags;
    uint fill_color, border_color;

    vec2 origin, rotation;
    float border_width;
    // vec3 _pad0;
};

layout(set = 0, binding = 1) readonly buffer InstanceBlock { QuadInstance2D instances[]; };
layout(set = 1, binding = 0) uniform sampler2D u_textures[32];

INOUT Varying {
    layout(location = 0) vec2 uv;
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
layout(push_constant) uniform Block {
    float center_size;
    float circle_width;
    float circle_distance;
    float cutout_size;
};

void main() {
    out_color.rgb = vec3(0.0); 

    float smoothness = 0.005;
    float circle = 1.0 - smoothstep(center_size * 0.1 - smoothness, center_size *0.1 +smoothness, distance(v.uv, vec2(0.5)));
    float outer_circle = 1.0 - smoothstep(circle_distance - smoothness, circle_distance + smoothness, distance(v.uv, vec2(0.5)));
    float inner_circle = 1.0 - smoothstep(circle_distance - circle_width - smoothness, circle_distance - circle_width + smoothness, distance(v.uv, vec2(0.5)));

    float dist_bands = 1.0 - (step(distance(v.uv.x, 0.5), cutout_size) + step(distance(v.uv.y, 0.5), cutout_size));
    out_color.a = circle  + ((outer_circle - inner_circle) * dist_bands);
}

// --- source_end ---