#version 450
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform sampler2D u_texture;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(u_texture, in_uv);

    if (color.a < 0.1) discard;

    out_color = color;
}
