#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "global.shared"
#include "vertex.shared"

layout(push_constant) uniform constants {
    mat4 model;
} push;

layout(location = 0) out vec3 out_position;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

void main() {
    gl_Position = global.projection * global.view * push.model * vec4(in_position.xyz, 1.0f);
    out_position = in_position.xyz;
    out_normal = mat3(transpose(inverse(push.model))) * in_normal.xyz;
    out_uv = in_uv;
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 0, binding = 0) uniform GlobalParameters {
    mat4 projection;
    mat4 view;
    vec4 camera_position;
} global;

layout(set = 0, binding = 1) readonly buffer LightData {
    vec4 lights[];
};

layout(set = 1, binding = 0) uniform MaterialParameters {
    vec4 base_color_factor;
    vec3 emissive_factor;
	float metallic_factor;
	float roughness_factor;
} material;
layout(set = 1, binding = 1) uniform sampler2D u_base_color_texture;
layout(set = 1, binding = 2) uniform sampler2D u_metallic_roughness_texture;
layout(set = 1, binding = 3) uniform sampler2D u_normal_texture;
layout(set = 1, binding = 4) uniform sampler2D u_occlusion_texture;
layout(set = 1, binding = 5) uniform sampler2D u_emissive_texture;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uint out_id;

layout(push_constant) uniform constants {
    mat4 model;
    uint node_id;
} push;

void main() {
    vec4 color = texture(u_base_color_texture, in_uv) * material.base_color_factor;

    vec3 normal = normalize(in_normal);
    vec3 light_direction = normalize(lights[0].xyz - in_position);
    vec3 reflection = reflect(-light_direction, normal);
    vec3 view_direction = normalize(global.camera_position.xyz - in_position);

    float ambient_strength = 0.05f;
    float specular_strength = 0.5f;

    vec3 ambient = vec3(ambient_strength);

    float diffuse_factor = max(dot(normal, light_direction), 0.0) * lights[0].w;
    vec3 diffuse = vec3(diffuse_factor) * lights[0].w;

    float specular_factor = pow(max(dot(view_direction, reflection), 0.0), 32);
    vec3 specular = specular_strength * specular_factor * vec3(1.0f);

    color = vec4((ambient + diffuse + specular) * color.xyz, color.a);

    out_color = color;
    out_id = push.node_id;
}

#endif
