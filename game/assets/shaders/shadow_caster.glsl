#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform GlobalParameters {
    mat4 light_matrix;
} global;

layout(push_constant) uniform constants {
    mat4 model;
} push;

layout(location = 0) in vec3 in_position;

void main() {
    gl_Position = global.light_matrix * push.model * vec4(in_position, 1.0f);
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)
void main() {}

#endif
