shader Composite {
    pipeline default(cull_mode = back, depth_test = false, depth_write = false)

    shared {
        layout(set = 0, binding = 0) uniform sampler2D textures[2];

        INOUT Varying {
            layout(location = 0) vec2 uv;
        } v;
    }

    vertex {
        const vec2 corners[4] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
        const uint indices[6] = { 0, 2, 3, 0, 3, 1 };

        void main() { 
            vec2 coord = corners[indices[gl_VertexIndex]];
            gl_Position = vec4(coord, 0.0, 1.0);
            v.uv = (coord + 1) * 0.5;
        }
    }

    fragment {
        layout(location = 0) out vec4 out_color;

        void main() {
            vec4 background = texture(textures[0], v.uv);
            vec4 foreground = texture(textures[1], v.uv);

            background.rgb = pow(background.rgb, vec3(1.0 / 2.2));

            out_color.rgb = background.rgb * (1.0 - foreground.a) + foreground.rgb;
            out_color.a = 1.0; 
        }
    }
}
