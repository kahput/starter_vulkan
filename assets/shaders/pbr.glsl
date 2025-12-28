#version 450

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
    float _pad0;
} u_scene;

layout(push_constant) uniform constants {
    mat4 model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_normal;

void main() {
    gl_Position = u_scene.projection * u_scene.view * push_constants.model * vec4(in_position, 1.0f);

    // vec3 T = normalize(vec3(push_constants.model * vec4(in_tangent.xyz, 0.0)));
    // vec3 N = normalize(vec3(push_constants.model * vec4(in_normal, 0.0)));
    // T = normalize(T - dot(T, N) * N);
    // vec3 B = cross(N, T) * in_tangent.w;
    //
    // mat3 TBN = mat3(T, B, N);

    out_uv = in_uv;
    out_normal = in_normal;
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT

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
} u_material;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 out_color;

void main() {
    // TODO: PBR
    vec4 albedo = texture(u_base_color_texture, uv) * u_material.base_color_factor;
    vec3 normal_sample = texture(u_normal_texture, uv).rgb * 2.0 - 1.0;

    vec4 mr_sample = texture(u_metallic_roughness_texture, uv);
    float roughness = mr_sample.g * u_material.roughness_factor;
    float metallic = mr_sample.b * u_material.metallic_factor;

    float ao = texture(u_occlusion_texture, uv).r;
    vec3 emissive = texture(u_emissive_texture, uv).rgb * u_material.emissive_factor;

    // out_color = vec4(normal, 1.0);

    vec3 ambient = vec3(.5) * albedo.rgb * ao;
    vec3 color = ambient + emissive;

    out_color = vec4(color, albedo.a);
}
#endif
