#version 450
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform Camera {
    mat4 view;
    mat4 projection;
} u_camera;

layout(push_constant) uniform constants {
    mat4 model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
// layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec2 uv;
// layout(location = 1) out vec3 normal;

void main() {
    gl_Position = u_camera.projection * u_camera.view * push_constants.model * vec4(in_position, 1.0f);
    uv = in_uv;
    // normal = in_normal;
}
