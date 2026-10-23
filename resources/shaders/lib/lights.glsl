struct Light {
    vec4 position;
    vec4 color;
    mat4 matrix;
};
layout(set = 0, binding = 1) readonly buffer LightBlock {
    Light lights[];
};
