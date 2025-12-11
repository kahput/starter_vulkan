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
    gl_Position = u_scene.projection * u_scene.view * push_constants.model * vec4(in_position, 1.0f);
    out_uv = in_uv;
    // normal = in_normal;
}
