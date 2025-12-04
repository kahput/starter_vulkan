#version 450
#pragma shader_stage(fragment)

// layout(set = 0, binding = 1) uniform sampler2D texture_sampler;

layout(location = 0) in vec2 uv;
// layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = vec4(0.1f, 0.1f, 0.1f, 1.0f);
    // out_color = texture(texture_sampler, uv);
}
