shader Shadow {
    pipeline default { }

    shared {
        #include "lib/frame.glsl"

        layout(set = 1, binding = 0) readonly buffer VertexBlock {
            Vertex3 vertex_buffer[];
        };

        layout(push_constant) uniform ConstantBlock {
            mat4 model;
        } pc;
    }

    vertex {
        void main() {
            Vertex3 vertex = vertex_buffer[gl_VertexIndex];
            gl_Position = frame.view * pc.model * vec4(vertex.position.xyz, 1.0);
        }
    }

    fragment {
        void main() {
        }
    }
}

shader Spatial {
    pipeline default { 
        cull = back;
    } 

    pipeline blended {
        blend = sub(one, one);
        cull = front;
        depth_test = false;
    }

    shared {
        #include "lib/frame.glsl"
        #include "lib/shadow.glsl"

        #define TEXTURE_ALBEDO 0
        #define TEXTURE_METAL_ROUGHNESS 1
        #define TEXTURE_NORMAL 2
        #define TEXTURE_OCCLUSION 3
        #define TEXTURE_EMISSIVE 4
        #define TEXTURE_COUNT 5

        layout(set = 1, binding = 0) readonly buffer VertexBlock {
            Vertex3 vertices[];
        } draw;
        layout(set = 1, binding = 1) uniform sampler2D u_textures[TEXTURE_COUNT];

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
        void main() {
            Vertex3 vertex = draw.vertices[gl_VertexIndex];

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

shader Transparent {
    pipeline default {
        blend = add(src_alpha, one_minus_src_alpha);
        cull = none;
    }

    shared Spatial;
    vertex Spatial;

    fragment { 
        layout(location = 0) out vec4 out_color;

        void main() {
            if (abs(dot(v.normal, vec3(0.0, 1.0, 0.0))) > 0.95) discard;

            float t = abs(cos( (v.uv.y + frame.time * 0.2) * TAU * 2.0));

            /* out_color =  vec4(vec3(t), smoothstep(0.00, 0.5, t) * pow(1.0 - v.uv.y, 3.0)); */
            // out_color = vec4(vec3(smoothstep(0.00, 0.5, t)), 1.0);
        }
    }
} 

shader Grass {

    shared {
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
    }

    vertex {
        void main() {
            Vertex3 vertex = vertex_buffer[gl_VertexIndex];
            mat4 transform = instances[gl_InstanceIndex];

            v.worldspace = vec3(transform * vec4(vertex.position.xyz, 1.0));
            if (pc.use_heightmap == 1)
                v.worldspace.y += (texture(u_heightmap, (v.worldspace.xz / 256.0) + 0.5).r - 0.5) * 40;
            v.lightspace = lights[0].matrix * vec4(v.worldspace, 1.0f);

            v.normal = vec3(0.0, 1.0, 0.0);
            v.uv = transform_uv(vertex.uv, pc.uv_st);

            float dist = length(vec3(frame.view * vec4(v.worldspace, 1.0)));
            v.fog_strength = exp(-pow(dist * frame.fog_density, frame.fog_gradient));
            v.fog_strength = clamp(v.fog_strength, 0.0, 1.0);

            gl_Position = frame.projection * frame.view * vec4(v.worldspace, 1.0);
        }
    }

    fragment {
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
    }
}

shader Skybox {

    shared {
        #include "lib/frame.glsl"

        INOUT Varying {
            layout (location = 0) vec3 worldspace;
        } v;
    }

    vertex {
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

        const int indices[36] = int[36](
                0, 1, 2, 2, 3, 0, // Front Face
                1, 5, 6, 6, 2, 1, // Right Face 
                5, 4, 7, 7, 6, 5, // Back Face 
                4, 0, 3, 3, 7, 4, // Left Face 
                3, 2, 6, 6, 7, 3, // Top Face 
                4, 5, 1, 1, 0, 4  // Bottom Face
                );

        void main() {
            vec3 position = positions[indices[gl_VertexIndex]];
            gl_Position = vec4(frame.projection * mat4(mat3(frame.view)) * vec4(position, 1.0)).xyww;
            v.worldspace = position;
        }
    }

    fragment {
        layout(location = 0) out vec4 out_color;

        void main() {
            out_color = texture(u_skybox, v.worldspace); 
        }
    }
}
