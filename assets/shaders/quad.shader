/* TODO:
material {
    META(
        @hint(color),
        @default(1.0, 1.0, 1.0, 1.0),
        @display("Tint")
    )
    float4 tint;

    META(
        @default(0.5), @range(0.0, 1.0), @display("Roughness")
    )
    float roughness;

    META(
        @default(0.0), @range(0.0, 10.0), @logarithmic
    )
    float emission;

    META(
      @hint(albedo), @default(white), @display("Albedo")
    )
    texture2d albedo;

    META( @hint(normal), @default(normal) );
    texture2D normal;
}
*/

shader Quad2D {
    pipeline default (
		  cull = none,
          depth_test = false,
          depth_write = false,

          blend_color = add(src_alpha, one_minus_src_alpha),
          blend_alpha = add(one, one_minus_src_alpha)
  )

    shared {
        #include "lib/frame.glsl"
        #extension GL_EXT_nonuniform_qualifier : enable

        struct QuadInstance2D {
            vec2 position, size;
            vec4 radii;
            vec2 uvs[4];

            uint imageid, flags;
            uint fill_color, border_color;

            vec2 origin, rotation;
            float border_width;
            // vec3 _pad0;
        };

        layout(set = 0, binding = 1) readonly buffer InstanceBlock { QuadInstance2D instances[]; };
        layout(set = 1, binding = 0) uniform sampler2D u_textures[32];

        INOUT Varying {
            layout(location = 0) vec2 uv;
            layout(location = 1) flat uint texture_id;

            layout(location = 2) vec4 fill_color;
            layout(location = 3) vec4 border_color;
            layout(location = 4) float border_width;

            layout(location = 5) vec2 size; 
            layout(location = 6) vec2 local; 
            flat layout(location = 7) vec4 radii;
        } v;
    }

    vertex { 
        const vec2 corners[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
        const uint indices[6] = { 0, 2, 3, 0, 3, 1 };

        void main() {
            QuadInstance2D quad = instances[gl_InstanceIndex];

            uint vertex_index = indices[gl_VertexIndex % 6];
            vec2 local = corners[vertex_index] * quad.size;
            vec2 rot_point = local - quad.origin;

            vec2 rotated = vec2(
                rot_point.x * quad.rotation.x - rot_point.y * quad.rotation.y,
                rot_point.x * quad.rotation.y + rot_point.y * quad.rotation.x
            );
            vec2 vertex_position = quad.position + quad.origin + rotated;

            gl_Position = frame.clip_from_view * frame.view_from_world * vec4(vertex_position, 0.0, 1.0);

            v.uv = quad.uvs[vertex_index];
            v.texture_id = quad.imageid;

            v.fill_color = unpackUnorm4x8(quad.fill_color);
            v.border_color = unpackUnorm4x8(quad.border_color);
            v.border_width = quad.border_width;

            v.size = quad.size;
            v.local = corners[vertex_index];
            v.radii = quad.radii;
        }
    }

    fragment {
        layout(location = 0) out vec4 out_color;
        float sd_rect_rounded(in vec2 p, in vec2 half_extent, in vec4 r) {
            // x: top-left, y: top-right, z: bottom-left, w: bottom-right
            r.xy = (p.x > 0.0) ? r.yw : r.xz;
            r.x = (p.y > 0.0) ? r.y : r.x;
            r = min(r, min(half_extent.x, half_extent.y));
            vec2 q = abs(p) - half_extent + r.x;
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r.x;
        }

        void main() {
            vec4 sampled = texture(u_textures[v.texture_id], v.uv);
            vec4 fill = sampled * v.fill_color;
            vec4 border = v.border_color;

            vec2 p = (v.local - 0.5) * v.size;
            vec2 e = v.size * 0.5;
            float dist = sd_rect_rounded(p, e, v.radii);
            float aa = max(fwidth(dist), 1e-4);

            float inner = 1.0, ring = 0.0;
            float outer = 1.0 - smoothstep(-aa, aa, dist);

            inner = 1.0 - smoothstep(-aa, aa, dist+v.border_width);
            ring = clamp(outer - inner, 0.0, 1.0);

            float fa = fill.a   * inner;
            float ba = border.a * ring;
            float a = fa + ba;

            vec3 premultiplied = fill.rgb * fa + border.rgb * ba;
            out_color.rgb = premultiplied / max(a, 1e-6);
            out_color.a   = a;
        }
    }
}

shader Quad2D_Experiment {
    pipeline default (
		  cull = none,
          depth_test = false,
          depth_write = false,

          blend = add(src_alpha, one_minus_src_alpha)
    )

    shared Quad2D;
    vertex { 
        const vec2 corners[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
        const uint indices[6] = { 0, 2, 3, 0, 3, 1 };

        void main() {
            uint vertex_index = indices[gl_VertexIndex % 6];
            QuadInstance2D quad = instances[gl_InstanceIndex];

            vec2 uv = corners[vertex_index];
            vec2 local = corners[vertex_index] * quad.size;
            vec2 vertex_position = quad.position + local;

            gl_Position = frame.clip_from_view * frame.view_from_world * vec4(vertex_position, 0.0, 1.0);

            v.uv = quad.uvs[vertex_index];
            v.texture_id = quad.imageid;
        }
    }

    fragment {
        layout(location = 0) out vec4 out_color;
        layout(push_constant) uniform Block {
            float center_size;
            float circle_width;
            float circle_distance;
            float cutout_size;
        };

        void main() {
            out_color.rgb = vec3(0.0); 

            float smoothness = 0.005;
            float circle = 1.0 - smoothstep(center_size * 0.1 - smoothness, center_size *0.1 +smoothness, distance(v.uv, vec2(0.5)));
            float outer_circle = 1.0 - smoothstep(circle_distance - smoothness, circle_distance + smoothness, distance(v.uv, vec2(0.5)));
            float inner_circle = 1.0 - smoothstep(circle_distance - circle_width - smoothness, circle_distance - circle_width + smoothness, distance(v.uv, vec2(0.5)));

            float dist_bands = 1.0 - (step(distance(v.uv.x, 0.5), cutout_size) + step(distance(v.uv.y, 0.5), cutout_size));
            out_color.a = circle  + ((outer_circle - inner_circle) * dist_bands);
        }
    }
}
