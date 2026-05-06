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

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_world_position;

void main() {
    vec3 world_position = vec3(push.model[3][0], push.model[3][1], push.model[3][2]);

    float scale_x = length(vec3(push.model[0]));
    float scale_y = length(vec3(push.model[1]));

    vec3 camera_direction = normalize(vec3(global.camera_position) - world_position);
    camera_direction.y = 0.0f;

    vec3 up = vec3(0.0f, 1.0f, 0.0f);
    vec3 right = normalize(cross(up, camera_direction));
    vec3 forward = normalize(camera_direction);

    mat3 billboard_matrix = mat3(right * scale_x, up * scale_y, forward);
    mat3 normal_matrix = mat3(right, up, forward);

    vec3 vertex_position = (billboard_matrix * vec3(in_position)) + world_position;

    gl_Position = global.projection * global.view * vec4(vertex_position, 1.0f);

    out_uv = in_uv;
    out_normal = normal_matrix * vec3(in_normal);
    out_world_position = world_position;
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform sampler2D u_texture;
layout(set = 1, binding = 1) uniform MaterialParameters {
    vec4 tint;
} material;

#include "global.shared"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_world_position;

layout(location = 0) out vec4 out_color;


void main() {
    vec4 albedo = texture(u_texture, in_uv) * material.tint;
    if (albedo.a < 0.1) discard;

    out_color = albedo;
}

#endif
