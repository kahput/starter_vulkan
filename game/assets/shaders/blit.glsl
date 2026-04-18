#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(location = 0) out vec2 out_uv;

const vec4 quad_vertices[4] = {
    // positions    uvs
    { -1.0f, -1.0f, 0.0f, 0.0f },
    { -1.0f,  1.0f, 0.0f, 1.0f },
    {  1.0f, -1.0f, 1.0f, 0.0f },
    {  1.0f,  1.0f, 1.0f, 1.0f },
};

const uint quad_indices[6] = {
    0, 1, 2,
    2, 1, 3
};

void main() {
    vec4 vertex = quad_vertices[quad_indices[gl_VertexIndex]];
    gl_Position = vec4(vertex.xy, 0.0f, 1.0f);
    out_uv = vertex.zw;
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
}

#endif
