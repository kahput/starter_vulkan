#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(push_constant) uniform constants {
    mat4 light_matrix;
    mat4 object;
} push;

layout(location = 0) in vec3 in_position;

void main() {
    gl_Position = push.light_matrix * push.object * vec4(in_position, 1.0f);
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)
void main() {}

#endif
