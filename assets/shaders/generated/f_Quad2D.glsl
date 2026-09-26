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
float sd_rect_rounded(in vec2 p, in vec2 half_extent, in vec4 r) {
    // x: top-left, y: top-right, z: bottom-left, w: bottom-right
    r.xy = (p.x > 0.0) ? r.yw : r.xz;
    r.x = (p.y > 0.0) ? r.y : r.x;
    r = min(r, min(half_extent.x, half_extent.y));
    vec2 q = abs(p) - half_extent + r.x;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
}

void main() {
    vec4 sampled = texture(u_textures[v.texture_id], v.tex_coords);
    vec4 fill = sampled * v.fill_color;
    vec4 border = v.border_color;

    vec2 p = (v.local - 0.5) * v.size;
    vec2 e = v.size * 0.5;

    float outer = 1.0, ring = 0.0;
    if (v.border_width > 0.0 || any(greaterThan(v.radii, vec4(0.0)))) { 
        float dist = sd_rect_rounded(p, e, v.radii);
        float aa = max(fwidth(dist), 1e-4);
        outer = 1.0 - smoothstep(-aa, aa, dist);

        if (v.border_width > 0.0) {
            float inner = 1.0 - smoothstep(-aa, aa, dist+v.border_width);
            ring = clamp(outer - inner, 0.0, 1.0);
        }
    }

    out_color.rgb = mix(mix(border.rgb, fill.rgb, fill.a), border.rgb, ring);
    float fa = fill.a * outer, ba = border.a * ring;
    out_color.a = ba + fa * (1.0 - ba);
}

// --- source_end ---