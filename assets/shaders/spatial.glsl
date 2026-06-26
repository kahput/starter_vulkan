struct Vertex3 {
    vec4 position; 
    vec4 normal;
    vec2 uv;
    vec4 tangent;
};

struct Light {
    vec4 position;
    vec4 color;
    mat4 matrix;
};

layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 projection;
    vec4 camera_position;
    vec2 viewport;
    float fog_density;
    float ambient;
    float fog_gradient;
    float time;
} frame;

layout(set = 0, binding = 1) readonly buffer LightBlock {
    Light lights[];
};

layout(set = 0, binding = 2) uniform sampler2DShadow u_shadow;
layout(set = 0, binding = 3) uniform samplerCube u_skybox;
