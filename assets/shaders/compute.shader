shader TestCompute {
    compute {
        //size of a workgroup for compute
        layout (local_size_x = 16, local_size_y = 16) in;

        //descriptor bindings for the pipeline
        layout(rgba16f,set = 0, binding = 0) uniform image2D image;

        ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
        ivec2 resolution = imageSize(image);

        layout(push_constant) uniform ConstantBlock {
            vec2 mouse;
            float time;
        } pc;

        vec4 srgb_to_linear(vec4 c) {
            bvec3 cutoff = lessThanEqual(c.rgb, vec3(0.04045));

            vec3 lower = c.rgb / 12.92;
            vec3 upper = pow((c.rgb + 0.055) / 1.055, vec3(2.4));

            return vec4(mix(upper, lower, cutoff), c.a);
        }


        void main() {
            vec2 st = vec2(texelCoord.x, texelCoord.y) / vec2(resolution);
            st.y = 1.0 - st.y;

            bool ok = texelCoord.x < resolution.x && texelCoord.y < resolution.y;
            if (ok) {
                vec4 color = vec4(vec3(pow(st.x, 5.0)), 1.0);
                vec3 pct = vec3(smoothstep(0.00, 0.01, abs(st.y-pow(st.x, 5.0))));

                color.rgb = pct * color.rgb + (1.0 - pct) * vec3(0.0, 1.0, 0.0);
                imageStore(image, texelCoord, srgb_to_linear(color));
            }
        }
    }
}

shader Skinning {
    compute {
        #extension GL_EXT_buffer_reference: require

        // size of a workgroup for compute
        layout (local_size_x = 256, local_size_y = 1) in;

        struct SkinningBuffer {
            ivec4 id;
            vec4 weight;
        };

        struct VertexBuffer {
            vec4 position; 
            vec4 normal;
            vec2 uv;
            vec4 tangent;
        };

        layout(buffer_reference, std430) readonly buffer VertexBufferBlock {
            VertexBuffer vertex_data[];
        };

        layout(buffer_reference, std430) readonly buffer SkinningBufferBlock {
            SkinningBuffer skinning_data[];
        };

        layout(buffer_reference, std430) readonly buffer SkinningMatrices {
            mat4 matrices[];
        };

        layout(push_constant) uniform ConstantBlock {
            uint vertex_count;
            SkinningMatrices transform;
            VertexBufferBlock input_buffer;
            SkinningBufferBlock skinning_buffer;
            VertexBufferBlock output_buffer;
        };

        void main() {
            uint vertex_index = gl_GlobalInvocationID.x;
            if (vertex_index >= vertex_count) return;

            SkinningBuffer sb = skinning_buffer.skinning_data[vertex_index]; 

            vec4 position = vec4(input_buffer.vertex_data[vertex_index].position.xyz, 1.0f);
            vec4 normal = vec4(input_buffer.vertex_data[vertex_index].normal.xyz, 0.0f);

            vec4 skinned_position = 
                sb.weight[0] * transform.matrices[sb.id[0]] * position + 
                sb.weight[1] * transform.matrices[sb.id[1]] * position + 
                sb.weight[2] * transform.matrices[sb.id[2]] * position + 
                sb.weight[3] * transform.matrices[sb.id[3]] * position;
            vec4 skinned_normal = 
                sb.weight[0] * transform.matrices[sb.id[0]] * normal + 
                sb.weight[1] * transform.matrices[sb.id[1]] * normal + 
                sb.weight[2] * transform.matrices[sb.id[2]] * normal + 
                sb.weight[3] * transform.matrices[sb.id[3]] * normal;

            output_buffer.vertex_data[vertex_index] = input_buffer.vertex_data[vertex_index];

            output_buffer.vertex_data[vertex_index].position = vec4(skinned_position.xyz, 1.0f);
            output_buffer.vertex_data[vertex_index].normal = vec4(skinned_normal.xyz, 0.0f);
        }
    }
}
