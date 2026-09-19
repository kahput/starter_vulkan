shader Spatial {
    pipeline default { } // defaults
    pipeline blended {
        blend = add(one, one);
        cull = none;
    }

    shared {
        #include "lib/frame"
        #include "lib/shadow"

        layout(push_constant) uniform ConstantBlock {
            mat4 model;
            vec4 tint;
            vec4 emissive;
            vec2 metallic_roughness;
            vec4 uv_st;
        } pc;

        INOUT Varying { // INOUT inside vertex = out, INOUT inside fragment = in
            layout (location = 0) vec3 worldspace;
            layout (location = 1) vec3 normal;
            layout (location = 2) vec2 uv;
            layout (location = 3) vec4 lightspace;
            layout (location = 4) float fog_strength;
        } v;
    }

    vertex { 
        layout(set = 1, binding = 0) readonly buffer VertexBlock {
            Vertex3 vertex_buffer[];
        };

        void main() {
            Vertex3 vertex = vertex_buffer[gl_VertexIndex];

            v.worldspace = vec3(pc.model * vec4(vertex.position.xyz, 1.0));
            v.lightspace = lights[0].matrix * vec4(v.worldspace, 1.0f);

            v.normal = mat3(transpose(inverse(pc.model))) * vertex.normal.xyz;
            v.uv = transform_uv(vertex.uv, pc.uv_st);

            float dist = length(vec3(frame.view * vec4(v.worldspace, 1.0)));
            v.fog_strength = exp(-pow(dist * frame.fog_density, frame.fog_gradient));
            v.fog_strength = clamp(v.fog_strength, 0.0, 1.0);

            gl_Position = frame.projection * frame.view * vec4(v.worldspace, 1.0);
        }
    }

    fragment { 
        #define TEXTURE_ALBEDO 0
        #define TEXTURE_METAL_ROUGHNESS 1
        #define TEXTURE_NORMAL 2
        #define TEXTURE_OCCLUSION 3
        #define TEXTURE_EMISSIVE 4
        #define TEXTURE_COUNT 5
        layout(set = 1, binding = 1) uniform sampler2D u_textures[TEXTURE_COUNT];

        layout(location = 0) out vec4 out_color;

        void main() {
            vec4 albedo = texture(u_textures[TEXTURE_ALBEDO], v.uv) * pc.tint;

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
    }
}

shader Transparent { // could be separate file
    pipeline default {
        blend_color = add(one, one);
        blend_alpha = sub(one, one_minus_src_alpha);
        cull = none;
    }

    vertex {}
    fragment { }
} 
