#version 450
#pragma shader_stage(vertex)

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 fragment_color;

void main() {
    gl_Position = vec4(in_position, 0.0f, 1.0f);
    fragment_color = in_color;
}
