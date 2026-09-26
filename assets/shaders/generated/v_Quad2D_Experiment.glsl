#version 450 core
#pragma shader_stage(vertex)

#define INOUT out

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

const vec2 corners[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
const uint indices[6] = { 0, 2, 3, 0, 3, 1 };

void main() {
    uint vertex_index = indices[gl_VertexIndex % 6];
    Quad2D quad = instances[gl_InstanceIndex];

    float t = sin(frame.time * 3.0);

    vec2 uv = corners[vertex_index];
    vec2 local = (uv - 0.5) * quad.size;

    local.x += (t * 0.5 + 0.5) * quad.size.x;

    vec2 rotated = vec2(
        local.x * quad.rotation.x - local.y * quad.rotation.y,
        local.x * quad.rotation.y + local.y * quad.rotation.x
    );

    vec2 world = quad.position + rotated;
    gl_Position = frame.projection * frame.view * vec4(world, 0.0, 1.0);

    v.tex_coords = uv;
    v.texture_id = quad.imageid;
}

// --- source_end ---