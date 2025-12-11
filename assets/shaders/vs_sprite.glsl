#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
} u_scene;

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

    vec3 camera_direction = normalize(u_scene.camera_position - world_position);
    camera_direction.y = 0.0f;

    vec3 up = vec3(0.0f, 1.0f, 0.0f);
    vec3 right = normalize(cross(up, camera_direction));
    vec3 forward = normalize(camera_direction);

    mat3 billboard_rotation = mat3(right, up, forward);

    vec3 vertex_position = (billboard_rotation * in_position) + world_position;

    gl_Position = u_scene.projection * u_scene.view * vec4(vertex_position, 1.0f);
    out_uv = in_uv;
}
