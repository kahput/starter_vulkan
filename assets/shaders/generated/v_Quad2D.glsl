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
    Quad2D quad = instances[gl_InstanceIndex];

    uint vertex_index = indices[gl_VertexIndex % 6];
    vec2 local = corners[vertex_index] * quad.size;
    vec2 rot_point = local - quad.origin;

    vec2 rotated = vec2(
            rot_point.x * quad.rotation.x - rot_point.y * quad.rotation.y,
            rot_point.x * quad.rotation.y + rot_point.y * quad.rotation.x
            );
    vec2 vertex_position = quad.position + quad.origin + rotated;

    gl_Position = frame.projection * frame.view * vec4(vertex_position, 0.0f, 1.0f);

    v.tex_coords = quad.uvs[vertex_index];
    v.texture_id = quad.imageid;

    v.fill_color = unpackUnorm4x8(quad.fill_color);
    v.border_color = unpackUnorm4x8(quad.border_color);
    v.border_width = quad.border_width;

    v.size = quad.size;
    v.local = corners[vertex_index];
    v.radii = quad.radii;
}

// --- source_end ---