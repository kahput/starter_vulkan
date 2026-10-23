shader Unlit {
    pipeline default ( cull = none )
    pipeline wireframe ( cull = none, polygon_mode = line )

    shared {
        #include "lib/frame.glsl"
        struct Vertex {
            vec3 position;
            vec3 normal;
            vec2 uv0, uv1;
        };
        layout(set = 1, binding = 0) readonly buffer VertexBlock {
            Vertex vertex_buffer[];
        };

        layout(push_constant) uniform ConstantBlock {
            mat4 world_from_local;
        };

        INOUT layout(location = 0) Varying {
            vec3 normal;
            vec2 uv;
        };
    }

    vertex {
        void main(void) {
            Vertex vertex = vertex_buffer[gl_VertexIndex];

            gl_Position = frame.clip_from_view * frame.view_from_world * world_from_local * vec4(vertex.position, 1.0);
            uv = vertex.uv0;
            normal = vertex.normal;
        }
    }

    fragment {
        layout(location = 0) out vec4 color;

        void main(void) {
            color = vec4(1.0);
        }
    }
}

