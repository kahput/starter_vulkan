#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "global.shared"

layout(push_constant) uniform constants {
    mat4 model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_position;

void main() {
    vec4 world_position = push_constants.model * vec4(in_position, 1.0f);
    gl_Position = u_scene.projection * u_scene.view * world_position;

    // vec3 T = normalize(vec3(push_constants.model * vec4(in_tangent.xyz, 0.0)));
    // vec3 N = normalize(vec3(push_constants.model * vec4(in_normal, 0.0)));
    // T = normalize(T - dot(T, N) * N);
    // vec3 B = cross(N, T) * in_tangent.w;
    //
    // mat3 TBN = mat3(T, B, N);

    out_uv = in_uv;
    out_normal = mat3(transpose(inverse(push_constants.model))) * in_normal;
    out_world_position = world_position.xyz;
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

#include "global.shared"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_world_position;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 albedo = texture(u_base_color_texture, in_uv) * u_material.base_color_factor;
    PointLight light = u_scene.light;

    float ambient_factor = 0.05f;
    vec3 ambient = ambient_factor * u_scene.light.color.rgb;
    vec3 normal = normalize(in_normal);
    vec3 light_direction = normalize(light.position.xyz - in_world_position);

    float diffuse_factor = max(dot(normal, light_direction), 0.0f);
    vec3 diffuse = diffuse_factor * light.color.rgb;

    float specular_strength = 0.0f;
    vec3 view_direction = normalize(u_scene.camera_position - in_world_position);
    vec3 reflection_vector = reflect(-light_direction, in_normal);

    float specular_factor = pow(max(dot(view_direction, reflection_vector), 0.0), 32);
    vec3 specular = specular_strength * specular_factor * light.color.rgb;

    vec3 color = (ambient + diffuse + specular) * albedo.rgb;

    vec3 debug_normal = in_normal * 0.5 + 0.5;
    out_color = vec4(color, albedo.a);
}
#endif
