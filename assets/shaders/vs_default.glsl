#version 450
#pragma shader_stage(vertex)

layout(binding = 0) uniform MVPObject {
    mat4 model;
    mat4 view;
    mat4 projection;
} mvp;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = mvp.projection * mvp.view * mvp.model * vec4(in_position, 1.0f);
    uv = in_uv;
}
