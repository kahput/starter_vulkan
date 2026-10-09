#define PI 3.14159265358979323846264338327950288
#define TAU PI * 2.0

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view_from_world;
    mat4 clip_from_view;
    vec4 camera_position;
    vec2 viewport;
    float fog_density;
    float ambient;
    float fog_gradient;
    float time;
} frame;

vec2 transform_uv(vec2 uv, vec4 uv_st) {
    return uv * uv_st.xy + uv_st.zw;
}
