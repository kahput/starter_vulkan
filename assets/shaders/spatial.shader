shader Unlit {
    pipeline default ( cull = none )

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

        INOUT layout(location = 0) Varying {
            vec2 uv;
        };
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
            color = vec4(vec3(uv.yyy), 1.0);
        }
    }
}
