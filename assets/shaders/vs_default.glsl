#version 450
#pragma shader_stage(vertex)

vec2 positions[6] = vec2[](
        vec2(-0.5f, -0.5f),
        vec2(0.5f, -0.5f),
        vec2(-0.5f, 0.5f),

        vec2(0.5f, -0.5f),
        vec2(0.5f, 0.5f),
        vec2(-0.5f, 0.5f)
    );

vec3 colors[6] = vec3[](
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, 0.0, 1.0),

        vec3(0.0, 1.0, 0.0),
        vec3(1.0, 0.0, 0.0),
        vec3(0.0, 0.0, 1.0)
    );

layout(location = 0) out vec3 fragment_color;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0f, 1.0f);
    fragment_color = colors[gl_VertexIndex];
}
