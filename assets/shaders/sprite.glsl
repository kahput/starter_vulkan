#version 450

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec3 camera_position;
} u_scene;

layout(push_constant) uniform constants {
    mat4 model;
} push_constants;

layout(location = 0) in vec3 in_position;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec2 out_uv;

void main() {
    vec3 world_position = vec3(push_constants.model[3][0], push_constants.model[3][1], push_constants.model[3][2]);

    vec3 camera_direction = normalize(u_scene.camera_position - world_position);
    camera_direction.y = 0.0f;

    vec3 up = vec3(0.0f, 1.0f, 0.0f);
    vec3 right = normalize(cross(up, camera_direction));
    vec3 forward = normalize(camera_direction);

    mat3 billboard_rotation = mat3(right, up, forward);

    vec3 vertex_position = (billboard_rotation * in_position) + world_position;

    gl_Position = u_scene.projection * u_scene.view * vec4(vertex_position, 1.0f);
    out_uv = in_uv;
}

#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform sampler2D u_texture;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(u_texture, in_uv);

    if (color.a < 0.1) discard;

    out_color = color;
}
#endif
