#version 450
#extension GL_GOOGLE_include_directive : require

// -----------------------------------------------------------------------------
// VERTEX SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_VERTEX
#pragma shader_stage(vertex)

#include "global.shared"

layout(set = 1, binding = 0) readonly buffer LineData {
    vec4 line_points[];
};

layout(push_constant) uniform constants {
    mat4 model;
} push;

const ivec2 quad[6] = ivec2[6](ivec2(0, -1), ivec2(0, 1), ivec2(1,  1),
        ivec2(0, -1), ivec2(1, 1), ivec2(1, -1) );

void main() {
    float width = global.viewport[0];
    float height = global.viewport[1];
    float aspect = width / height;
    mat4 mvp = global.projection * global.view * push.model;

    int line_offset = (gl_VertexIndex / 6) * 2;
    vec4 point0 = line_points[line_offset]; 
    vec4 point1 = line_points[line_offset + 1]; 

    vec4 clip_position0 = mvp * vec4(point0.xyz, 1.0f);
    vec4 clip_position1 = mvp * vec4(point1.xyz, 1.0f);

    vec2 ndc_position0 = clip_position0.xy / clip_position0.w;
    vec2 ndc_position1 = clip_position1.xy / clip_position1.w;

    vec2 line_vector = ndc_position1 - ndc_position0;
    vec2 direction = normalize(vec2(line_vector.x, line_vector.y * aspect)); 
    vec2 normal = vec2(-direction.y, direction.x);
    
    ivec2 quad_position = quad[gl_VertexIndex % 6];
    float current_thickness = mix(point0.w, point1.w, float(quad_position.x));

    vec4 vertex_position = mix(clip_position0, clip_position1, float(quad_position.x));

    vec2 offset = normal * (current_thickness / global.viewport.x);

    vertex_position.xy += offset * vertex_position.w * float(quad_position.y);

    gl_Position = vertex_position;
}
#endif

// -----------------------------------------------------------------------------
// FRAGMENT SHADER
// -----------------------------------------------------------------------------
#ifdef SHADER_STAGE_FRAGMENT
#pragma shader_stage(fragment)

layout(set = 1, binding = 1) uniform Material {
    vec4 color;
};

layout(location = 0) out vec4 out_color;

void main() {
    out_color = color; 
}

#endif
