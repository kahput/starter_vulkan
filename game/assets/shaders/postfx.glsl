#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

void main() {
    gl_Position = vec4(in_position.xy, 0.0f, 1.0f);
    out_uv = in_uv;
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 0, binding = 0) uniform sampler2D u_screen;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_screen, in_uv);
    // float average = 0.2126 * out_color.r + 0.7152 * out_color.g + 0.0722 * out_color.b;
    //
    // out_color = vec4(vec3(average), 1.0f);
}

#endif
