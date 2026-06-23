struct Vertex2 {
    vec2 position; 
    vec2 uv;
    vec4 color;
};

layout(set = 0, binding = 0) uniform Frame2D {
    mat4 view;
    mat4 projection;
    vec2 camera_position;
    vec2 viewport;
    float time;
} frame;
