#version 450
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform sampler2D u_base_color_texture;
layout(set = 1, binding = 1) uniform sampler2D u_metallic_roughness_texture;
layout(set = 1, binding = 2) uniform sampler2D u_normal_texture;
layout(set = 1, binding = 3) uniform sampler2D u_occlusion_texture;
layout(set = 1, binding = 4) uniform sampler2D u_emissive_texture;

layout(set = 1, binding = 5) uniform MaterialParameters {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    vec3 emissive_factor;
} u_material_parameters;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 out_color;

void main() {
    // TODO: PBR
    out_color = texture(u_base_color_texture, uv) * u_material_parameters.base_color_factor;
}
