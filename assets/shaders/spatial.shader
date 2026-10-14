shader Unlit {
    pipeline default ( cull = none )
    pipeline wireframe ( cull = none, polygon_mode = line )

    shared {
        #include "lib/frame.glsl"
        struct Vertex {
            vec3 position;
            vec3 normals;
            vec2 uv0, uv1;
        };
        layout(set = 1, binding = 0) readonly buffer VertexBlock {
            Vertex vertex_buffer[];
        };

        layout(set = 1, binding = 1) uniform MaterialBlock {
            float health;
        };
        layout(set = 1, binding = 2) uniform sampler2D u_healthbar;

        INOUT layout(location = 0) Varying {
            vec2 uv;
        };

        float inv_lerp(float a, float b, float v) {
            return (v - a) / (b - a);
        }
    }

    vertex {
        void main(void) {
            Vertex vertex = vertex_buffer[gl_VertexIndex];

            gl_Position = frame.clip_from_view * frame.view_from_world * vec4(vertex.position, 1.0);
            uv = vertex.uv0;
        }
    }

    fragment {
        layout(location = 0) out vec4 color;

        void main(void) {
            vec2 coords = uv;
            coords.x *= 8.0;

            vec2 point_on_segment = vec2(clamp(coords.x, 0.5, 7.5), 0.5);
            float dist = distance(coords, point_on_segment) * 2.0 - 1.0;
            if (dist > 0.0) discard;

            float border_sdf = dist + 0.3;
            float pd = fwidth(border_sdf);
            float border_mask = 1.0 - clamp(border_sdf / pd, 0.0, 1.0);
            
            vec4 sampled = texture(u_healthbar, vec2(health, uv.y));
            vec4 mask = vec4(vec3(float(health > uv.x)), 1.0);
            float flash = cos(frame.time * 8.0) * 0.6 + 1.0;

            if (health <= 0.2)
                sampled *= flash;

            color = vec4(sampled.xyz * mask.xyz * border_mask, 1.0) ;
        }
    }
}

