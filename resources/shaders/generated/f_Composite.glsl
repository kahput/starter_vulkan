#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

layout(set = 0, binding = 0) uniform sampler2D textures[2];

INOUT Varying {
    layout(location = 0) vec2 uv;
} v;

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 out_color;

void main() {
    vec4 background = texture(textures[0], v.uv);
    vec4 foreground = texture(textures[1], v.uv);

    background.rgb = pow(background.rgb, vec3(1.0 / 2.2));

    out_color.rgb = background.rgb * (1.0 - foreground.a) + foreground.rgb;
    out_color.a = 1.0; 
}

// --- source_end ---