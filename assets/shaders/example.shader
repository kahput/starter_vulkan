Shader "Spatial" {
    Pipeline { } // defaults
    Pipeline {
        Blend One One;
        Cull None;
    }

    Vertex { 
        layout(set = 1, binding = 0) readonly buffer VertexBlock {
            Vertex3 vertex_buffer[];
        };

        layout(push_constant) uniform ConstantBlock {
            mat4 model;
        } pc;

        void main() {
            Vertex3 vertex = vertex_buffer[gl_VertexIndex];
            gl_Position = frame.view * pc.model * vec4(vertex.position.xyz, 1.0);
        }
    }
    Fragment { }
}

Shader "Transparent" { // could be separate file
    Pipeline {
        BlendColor One One;
        BlendAlpha One One;
        Cull None;
    }

    Vertex "Spatial"
    Fragment { }
} 
