shader Line3D {
    pipeline default { }

    shared {
        #include "lib/frame.glsl"  

        struct LineInstance3D {
            vec4 a, b; // xyz + thickness
            uint color;
        };

        layout(set = 1, binding = 0) readonly buffer LineBlock {
            LineInstance3D instances[];
        };

        INOUT Varying {
            layout (location = 0) vec4 color;
        } v;
    }


    vertex { 
        const ivec2 quad[6] = ivec2[6](ivec2(0, -1), ivec2(0, 1), ivec2(1,  1),
                ivec2(0, -1), ivec2(1, 1), ivec2(1, -1) );

        void main() {
            float aspect = frame.viewport.x / frame.viewport.y;
            mat4 vp = frame.projection * frame.view;

            LineInstance3D line = instances[gl_InstanceIndex];

            vec3 start = line.a.xyz; 
            vec3 end = line.b.xyz; 

            vec4 clip_position0 = vp * vec4(start, 1.0f);
            vec4 clip_position1 = vp * vec4(end, 1.0f);

            vec2 ndc_position0 = clip_position0.xy / clip_position0.w;
            vec2 ndc_position1 = clip_position1.xy / clip_position1.w;

            vec2 line_vector = ndc_position1 - ndc_position0;
            vec2 direction = normalize(vec2(line_vector.x, line_vector.y * aspect)); 
            vec2 normal = vec2(-direction.y, direction.x);

            ivec2 quad_position = quad[gl_VertexIndex % 6];
            float current_thickness = mix(line.a.w, line.b.w, float(quad_position.x));
            vec4 vertex_position = mix(clip_position0, clip_position1, float(quad_position.x));

            vec2 offset = normal * (current_thickness / frame.viewport.x);

            vertex_position.xy += offset * vertex_position.w * float(quad_position.y);

            v.color = unpackUnorm4x8(line.color);
            gl_Position = vertex_position;
        }

    }
    fragment {
        layout(location = 0) out vec4 out_color;

        void main() {
            out_color = v.color; 
        }
    }
}
