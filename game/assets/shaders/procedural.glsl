#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalParameters {
    mat4 projection;
    mat4 view;
} global;

struct Vertex {
	vec3 position;
	vec3 normal;
	vec2 uv;
	vec4 tangent;
	vec4 color;
};

layout(set = 1, binding = 0) readonly buffer VertexData {
    Vertex data[];
};

layout(push_constant) uniform constants {
    mat4 model;
} push;

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = global.projection * global.view * push.model * vec4(data[gl_VertexIndex].position, 1.0f);
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(location = 0) out vec4 out_color;
layout(location = 1) out uint out_id;

layout(push_constant) uniform constants {
    mat4 model;
    uint id;
} push;

void main() {
    out_color = vec4(1.0f, 0.5f, 0.2f, 1.0f); // * material.tint;
    out_id = push.id;
}

#endif
