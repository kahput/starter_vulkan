#version 450 core
#pragma shader_stage(fragment)

#define INOUT in

// --- shared_start ---

#include "lib/frame.glsl"
#include "lib/shadow.glsl"

#define TEXTURE_ALBEDO 0
#define TEXTURE_METAL_ROUGHNESS 1
#define TEXTURE_NORMAL 2
#define TEXTURE_OCCLUSION 3
#define TEXTURE_EMISSIVE 4
#define TEXTURE_COUNT 5

layout(set = 1, binding = 0) readonly buffer VertexBlock {
    Vertex3 vertex_buffer[];
};
layout(set = 1, binding = 1) uniform sampler2D u_textures[TEXTURE_COUNT];
layout(set = 1, binding = 2) readonly buffer InstanceBlock {
    mat4 instances[];
};
layout(set = 1, binding = 3) uniform sampler2D u_heightmap;

layout(push_constant) uniform ConstantBlock {
    vec4 tint;
    vec4 emissive;
    vec2 metallic_roughness;
    vec4 uv_st;
    uint use_heightmap;
} pc;

INOUT Varying {
    layout (location = 0) vec3 worldspace;
    layout (location = 1) vec3 normal;
    layout (location = 2) vec2 uv;
    layout (location = 3) vec4 lightspace;
    layout (location = 4) float fog_strength;
} v;

// --- shared_end ---

// --- source_start ---

layout(location = 0) out vec4 out_color;

void main() {
    vec4 albedo = texture(u_textures[TEXTURE_ALBEDO], v.uv) * pc.tint;
    if (albedo.a < 0.75) discard;
    albedo.xyz *= (1 - v.uv.y) + 0.5;

    vec3 normal = normalize(v.normal);
    vec3 fragment_to_light = normalize(lights[0].position.xyz - v.worldspace);
    vec3 reflection = reflect(-fragment_to_light, normal);

    vec3 fragment_to_camera = normalize(frame.camera_position.xyz - v.worldspace);

    float specular_strength = pc.metallic_roughness.x;

    vec3 light_color = lights[0].color.rgb;
    vec3 ambient = vec3(frame.ambient) * light_color;

    float diffuse_factor = max(dot(normal, fragment_to_light), 0.0) ;
    vec3 diffuse = vec3(diffuse_factor) * light_color;

    float specular_factor = pow(max(dot(fragment_to_camera, reflection), 0.0), 32);
    vec3 specular = specular_strength * specular_factor * light_color;

    float bias = max(0.001 * (1.0 - dot(normal, fragment_to_light)), 0.0005);
    float visibility = calculate_shadow(v.lightspace, bias); 

    vec3 lighting = (ambient + visibility * (diffuse + specular));    

    vec3 view_ray = normalize(v.worldspace - frame.camera_position.xyz);
    vec4 skybox_color = texture(u_skybox, view_ray);

    vec3 c = mix(skybox_color.rgb, vec3(lighting * albedo.xyz), v.fog_strength);
    out_color = vec4(c, 1.0);
}

// --- source_end ---