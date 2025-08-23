#version 450
#pragma shader_stage(fragment)

layout(location = 0) in vec3 fragment_color;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(fragment_color, 1.0f);
}
