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
} push;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_position;

void main() {
    vec4 world_position = push.model * vec4(in_position, 1.0f);
    gl_Position = global.projection * global.view * world_position;

    // vec3 T = normalize(vec3(push.model * vec4(in_tangent.xyz, 0.0)));
    // vec3 N = normalize(vec3(push.model * vec4(in_normal, 0.0)));
    // T = normalize(T - dot(T, N) * N);
    // vec3 B = cross(N, T) * in_tangent.w;
    //
    // mat3 TBN = mat3(T, B, N);

    out_uv = in_uv;
    out_normal = mat3(transpose(inverse(push.model))) * in_normal;
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
} material;

#include "global.shared"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_world_position;

layout(location = 0) out vec4 out_color;

vec3 calculate_directional_light(DirectionalLight light, vec4 albedo, vec3 normal, vec3 view_direction);
vec3 calculate_point_light(PointLight light, vec4 albedo, vec3 normal, vec3 world_position, vec3 view_direction);

void main() {
    vec4 albedo = texture(u_base_color_texture, in_uv) * material.base_color_factor;

    vec3 normal = normalize(in_normal);
    vec3 view_direction = normalize(global.camera_position - in_world_position);

    vec3 result = calculate_directional_light(global.directional_light, albedo, normal, view_direction);

    for (uint index = 0; index < global.light_count; ++index)
        result += calculate_point_light(global.point_lights[index], albedo, normal, in_world_position, view_direction);

    vec3 debug_normal = in_normal * 0.5 + 0.5;
    out_color = vec4(result, albedo.a);
}

vec3 calculate_point_light(PointLight light, vec4 albedo, vec3 normal, vec3 world_position, vec3 view_direction) {
    vec4 roughness = texture(u_metallic_roughness_texture, in_uv) * material.roughness_factor;

    vec3 light_direction = normalize(light.position.xyz - world_position);

    float diffuse_factor = max(dot(normal, light_direction), 0.0);

    vec3 reflection = reflect(-light_direction, normal);
    float specular_factor = pow(max(dot(view_direction, reflection), 0.0), 32) * SPECULAR_STRENGTH;

    vec3 ambient = light.color.rgb * light.color.a * AMBIENT_STRENGTH * albedo.rgb;
    vec3 diffuse = light.color.rgb * light.color.a * diffuse_factor * albedo.rgb;
    vec3 specular = light.color.rgb * light.color.a * specular_factor * roughness.rgb;

    float distance = length(light.position.xyz - world_position);
    float attenuation = 1.0f / (ATTENUATION_CONSTANT + ATTENUATION_LINEAR * distance + ATTENUATION_QUADRATIC * (distance * distance));

    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;

    return ambient + diffuse + specular;
}

vec3 calculate_directional_light(DirectionalLight light, vec4 albedo, vec3 normal, vec3 view_direction) {
    vec4 roughness = texture(u_metallic_roughness_texture, in_uv) * material.roughness_factor;

    vec3 light_direction = normalize(-light.direction.xyz);

    float diffuse_factor = max(dot(normal, light_direction), 0.0);

    vec3 reflection = reflect(-light_direction, normal);
    float specular_factor = pow(max(dot(view_direction, reflection), 0.0), 32) * SPECULAR_STRENGTH;

    vec3 ambient = light.color.rgb * light.color.a * AMBIENT_STRENGTH * albedo.rgb;
    vec3 diffuse = light.color.rgb * light.color.a * diffuse_factor * albedo.rgb;
    vec3 specular = light.color.rgb * light.color.a * specular_factor * roughness.rgb;

    return ambient + diffuse + specular;
}

#endif
