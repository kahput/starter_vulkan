#version 450
#extension GL_GOOGLE_include_directive : require

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

void main() {
    vec3 world_position = vec3(push_constants.model[3][0], push_constants.model[3][1], push_constants.model[3][2]);

    float scale_x = length(vec3(push_constants.model[0]));
    float scale_y = length(vec3(push_constants.model[1]));

    vec3 camera_direction = normalize(global.camera_position - world_position);
    camera_direction.y = 0.0f;

    vec3 up = vec3(0.0f, 1.0f, 0.0f);
    vec3 right = normalize(cross(up, camera_direction));
    vec3 forward = normalize(camera_direction);

    mat3 billboard_rotation = mat3(right * scale_x, up * scale_y, forward);

    vec3 vertex_position = (billboard_rotation * in_position) + world_position;

    gl_Position = global.projection * global.view * vec4(vertex_position, 1.0f);

    out_uv = in_uv;
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT

#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform MaterialParameters {
    vec4 color;
} material;

#include "global.shared"

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 center_offset = in_uv * 2.0 - 1.0;

    float dist_squared = dot(center_offset, center_offset);

    if (dist_squared > 1.0)
        discard;

    out_color = material.color;
}
#endif
