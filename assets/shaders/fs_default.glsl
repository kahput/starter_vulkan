#version 450
#pragma shader_stage(fragment)

layout(binding = 1) uniform sampler2D texture_sampler;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;

layout(binding = 2) uniform Material {
    vec4 base_color_factor;
} material;

void main() {
    out_color = material.base_color_factor;
    // out_color = texture(texture_sampler, uv * 2.f);
}
