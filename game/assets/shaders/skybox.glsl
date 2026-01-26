#version 450
#extension GL_GOOGLE_include_directive : require

#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "global.shared"

layout(location = 0) out vec3 out_position;

// The 8 corners of a unit cube (-1 to 1)
const vec3 positions[8] = vec3[8](
    vec3(-1.0, -1.0,  1.0), // 0: Front-Bottom-Left
    vec3( 1.0, -1.0,  1.0), // 1: Front-Bottom-Right
    vec3( 1.0,  1.0,  1.0), // 2: Front-Top-Right
    vec3(-1.0,  1.0,  1.0), // 3: Front-Top-Left
    vec3(-1.0, -1.0, -1.0), // 4: Back-Bottom-Left
    vec3( 1.0, -1.0, -1.0), // 5: Back-Bottom-Right
    vec3( 1.0,  1.0, -1.0), // 6: Back-Top-Right
    vec3(-1.0,  1.0, -1.0)  // 7: Back-Top-Left
);

// 36 indices to form 12 triangles
const int indices[36] = int[36](
    // Front Face
    0, 1, 2, 2, 3, 0,
    // Right Face
    1, 5, 6, 6, 2, 1,
    // Back Face
    5, 4, 7, 7, 6, 5,
    // Left Face
    4, 0, 3, 3, 7, 4,
    // Top Face
    3, 2, 6, 6, 7, 3,
    // Bottom Face
    4, 5, 1, 1, 0, 4
);

void main() {
    // 1. Fetch the position directly from the constant arrays
    vec3 position = positions[indices[gl_VertexIndex]];
    
    // 2. Remove Translation from View Matrix inside the shader
    // We cast the view mat4 to a mat3 (taking only rotation/scale), 
    // and back to a mat4. This zeros out the translation column.
    // This saves you from having to manually copy/edit matrices on the CPU!
    mat4 rot_view = mat4(mat3(global.view));
    
    // 3. Calculate Clip Position
    vec4 clip_pos = global.projection * rot_view * vec4(position, 1.0);

    // 4. The "Infinite Sky" Depth Trick
    // We replace Z with W. When the perspective divide (z / w) happens later,
    // the result will be (w / w) = 1.0. 
    // This forces the skybox to always be at the maximum depth (the far plane).
    gl_Position = clip_pos.xyww;

    // 5. Pass the original local position as the UVW coordinate for the cubemap
    out_position = position;
}

#endif

#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 1, binding = 0) uniform samplerCube u_skybox;

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = texture(u_skybox, in_position);
}

#endif
