#include "draw.h"
#include "core/debug.h"

void draw2d_quad(Arena *arena, Rectangle dst, Rectangle src, Image2D *image, float2 origin, float rotation, float border_width, Color border_color, float4 radii, Color fill_color) {
	bool ok = arena;
	if (ok) {
		float2 size = { dst.width, dst.height };
		float2 min = sub2(make2(dst.x, dst.y), origin), max = add2(min, make2(dst.width, dst.height));
		float2 corners[] = { min, { max.x, min.y }, { min.x, max.y }, max };

		float2 uv0 = zero2;
		float2 uv1 = one2;
		if (image) {
			uv0 = make2(src.x / image->width, src.y / image->height);
			uv1 = make2((src.x + src.width) / image->width, (src.y + src.height) / image->height);
		}
		float2 uvs[] = { uv0, { uv1.x, uv0.y }, { uv0.x, uv1.y }, uv1 };

		if (rotation != 0.0f) {
			float2x2 rot = make2x2_from_rotation(DEG2RAD * rotation);
			min = negate2(origin);
			max = add2(min, make2(dst.width, dst.height));

			corners[0] = add2(make2(dst.x, dst.y), mul2x2v(rot, min));
			corners[1] = add2(make2(dst.x, dst.y), mul2x2v(rot, make2(max.x, min.y)));
			corners[2] = add2(make2(dst.x, dst.y), mul2x2v(rot, make2(min.x, max.y)));
			corners[3] = add2(make2(dst.x, dst.y), mul2x2v(rot, max));
		}

		uint32_t imageid = image ? image->handle->imageid : 0;
		// clang-format off
        QuadVertex2D quad[] = {
            // pos      // tex
            (QuadVertex2D){ .position = corners[0], .uv = uvs[0] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[2], .uv = uvs[2] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[3], .uv = uvs[3] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width },  

            (QuadVertex2D){ .position = corners[0], .uv = uvs[0] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[3], .uv = uvs[3] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
            (QuadVertex2D){ .position = corners[1], .uv = uvs[1] , .radii = radii, .size = size, .fill_color = color_pack_uint32(fill_color), .border_color = color_pack_uint32(border_color), .imageid = imageid, .border_width = border_width }, 
        };
		// clang-format on

		memory_copy(arena_push_count(arena, QuadVertex2D, 6), quad, sizeof(quad));
	}
}

void draw2d_rect_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, Color color) {
	draw2d_quad(arena, rect, rect(0, 0, rect.width, rect.height), 0, origin, rotation, 0.0f, rgba(0, 0, 0, 0), splat4(0.0f), color);
}

void draw2d_rect(Arena *arena, Rectangle rect, Color color) {
	draw2d_rect_ex(arena, rect, zero2, 0.0f, color);
}

void draw2d_rect_outline(Arena *arena, Rectangle rect, float thickness, Color color) {
	draw2d_quad(arena, rect, (Rectangle){ 0 }, 0, zero2, 0, thickness, color, splat4(0.0f), TRANSPARENT);
}

void draw2d_rect_rounded(Arena *arena, Rectangle rect, float4 radii, Color color) {
	draw2d_quad(arena, rect, rect(0, 0, rect.width, rect.height), 0, zero2, 0.0f, 0.0f, rgba(0, 0, 0, 0), radii, color);
}

void draw2d_rect_rounded_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, float4 radii, Color color) {
	draw2d_quad(arena, rect, rect(0.0f, 0.0f, rect.width, rect.height), 0, origin, rotation, 0.0f, rgba(0, 0, 0, 0), radii, color);
}

void draw2d_sprite_ex(Arena *arena, Rectangle src, Rectangle dst, Image2D *image, Color tint) {
	draw2d_quad(arena, dst, src, image, zero2, 0.0f, 0.0f, rgba(0, 0, 0, 0), splat4(0.0f), tint);
}

void draw2d_sprite(Arena *arena, float2 position, Image2D *image, Color tint) {
	draw2d_sprite_ex(arena, rect(0, 0, image->width, image->height), rect(position.x, position.y, image->width, image->height), image, tint);
}

void draw2d_point(Arena *arena, float2 position, float radius, Color color) {
	draw2d_quad(arena, rect(position.x, position.y, radius * 2.0, radius * 2.0), (Rectangle){ 0 }, 0, splat2(radius), 0.0, 0.0, TRANSPARENT, splat4(radius), color);
}

void draw2d_textf(Arena *arena, Font *font, float2 position, Color color, String8 format, ...) {
	ArenaTemp scratch = arena_scratch_begin(arena);

	bool ok = arena && font;
	if (ok) {
		va_list args;
		va_start(args, format);
		String8 text = str8_push_format_list(scratch.arena, format, args);
		va_end(args);

		float y_offset = 0.0f;
		for (uint32_t index = 0; index < text.length; ++index) {
			uint8_t c = text.text[index];
			Glyph *glyph = &font->glyphs[c];
			y_offset = maxf(y_offset, glyph->src.height);
		}

		float x_offset = 0.0f;
		for (uint32_t index = 0; index < text.length; ++index) {
			uint8_t c = text.text[index];
			if (c == '\n') {
				x_offset = 0.0f;
				y_offset += font->bake_size;
			}

			Glyph *glyph = &font->glyphs[c];

			Rectangle dst = {
				.x = position.x + x_offset + (glyph->bearing.x),
				.y = position.y + y_offset + (glyph->bearing.y),
				.width = glyph->src.width,
				.height = glyph->src.height,
			};

			draw2d_quad(arena, dst, glyph->src, &font->atlas, splat2(0.0f), 0.0f, 0.0f, TRANSPARENT, splat4(0.0f), color);

			x_offset += glyph->advance_x;
		}
	}

	arena_scratch_end(scratch);
}

void draw2d_triangle(Arena *arena, Triangle2 t, float thickness, Color color) {
	draw2d_line(arena, t.a, t.b, thickness, color);
	draw2d_line(arena, t.b, t.c, thickness, color);
	draw2d_line(arena, t.c, t.a, thickness, color);
}

void draw2d_line(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4(start.x, start.y, 0.0f, thickness),
		.b = make4(end.x, end.y, 0.0f, thickness),
		.color = color_pack_uint32(color),
	};
}

void draw2d_arrow(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	float2 shaft_end = add2(start, scale2(sub2(end, start), 0.75f));
	draw2d_line(arena, start, shaft_end, thickness, color);

	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4(shaft_end.x, shaft_end.y, 0.0f, thickness * 4.0f),
		.b = make4(end.x, end.y, 0.0f, 0.0f),
		.color = color_pack_uint32(color),
	};
}

void draw3d_arc(Arena *arena, float3 center, float radius, uint8_t segments, Side plane, float angle_span, float thickness, Color color) {
	for (uint32_t i = 0; i < segments; ++i) {
		float a = ((float)i / segments) * angle_span;
		float an = ((float)(i + 1) / segments) * angle_span;
		float ca = cosf(a), sa = sinf(a);
		float can = cosf(an), san = sinf(an);

		float3 p0, p1;
		switch (plane) {
			case SIDE_BOTTOM:
			case SIDE_TOP:
				p0 = (float3){ center.x + ca * radius, center.y, center.z + sa * radius };
				p1 = (float3){ center.x + can * radius, center.y, center.z + san * radius };
				break;
			case SIDE_LEFT:
			case SIDE_RIGHT:
				p0 = (float3){ center.x, center.y + sa * radius, center.z + ca * radius };
				p1 = (float3){ center.x, center.y + san * radius, center.z + can * radius };
				break;
			case SIDE_BACK:
			case SIDE_FRONT:
				p0 = (float3){ center.x + ca * radius, center.y + sa * radius, center.z };
				p1 = (float3){ center.x + can * radius, center.y + san * radius, center.z };
				break;

			default:
				ASSERT(false);
				break;
		}

		draw3d_line(arena, p0, p1, thickness, color);
	}
}

void draw3d_sphere_outline(Arena *arena, float3 center, float radius, uint8_t segments, float thickness, Color color) {
	draw3d_arc(arena, center, radius, segments, SIDE_TOP, TAU, thickness, color);
	draw3d_arc(arena, center, radius, segments, SIDE_RIGHT, TAU, thickness, color);
	draw3d_arc(arena, center, radius, segments, SIDE_FRONT, TAU, thickness, color);
}

void draw3d_capsule_outline(Arena *arena, float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color) {
	LineVertex3D *spine_points = arena_push_count(arena, LineVertex3D, 8);
	LineVertex3D spine[] = {
		{ { a.x - radius, a.y, a.z, thickness }, { a.x - radius, b.y, a.z, thickness }, color_pack_uint32(WHITE), zero3 },
		{ { a.x + radius, a.y, a.z, thickness }, { a.x + radius, b.y, a.z, thickness }, color_pack_uint32(WHITE), zero3 },
		{ { a.x, a.y, a.z - radius, thickness }, { a.x, b.y, a.z - radius, thickness }, color_pack_uint32(WHITE), zero3 },
		{ { a.x, a.y, a.z + radius, thickness }, { a.x, b.y, a.z + radius, thickness }, color_pack_uint32(WHITE), zero3 },
	};
	memory_copy_array(spine_points, spine);

	for (uint32_t end = 0; end < 2; ++end) {
		float3 c = end == 0 ? a : b;
		float signed_r = (end == 0) ? -radius : radius;

		draw3d_arc(arena, c, radius, segments, SIDE_TOP, TAU, thickness, WHITE);
		draw3d_arc(arena, c, signed_r, segments, SIDE_RIGHT, PIf, thickness, WHITE);
		draw3d_arc(arena, c, signed_r, segments, SIDE_FRONT, PIf, thickness, WHITE);
	}
}

void draw3d_aabb_outline(Arena *arena, AABB3 aabb3, float thickness, Color color) {
	float3 min = aabb3.min;
	float3 max = aabb3.max;
	float3 bounding_box_size = sub3(max, min);

	LineVertex3D outline[] = {
		{ { min.x, min.y, min.z, thickness }, { min.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, min.y, max.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, min.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, max.z, thickness }, { max.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, min.y, min.z, thickness }, { min.x, min.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, min.y, min.z, thickness }, { max.x, min.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, max.z, thickness }, { max.x, min.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, min.y, max.z, thickness }, { min.x, min.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, max.y, min.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
		{ { min.x, max.y, min.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, max.y, max.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), zero3 },
		{ { max.x, max.y, max.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), zero3 },
	};

	LineVertex3D *points = arena_push_count(arena, LineVertex3D, countof(outline));
	memory_copy_array(points, outline);
}

void draw3d_line(Arena *arena, float3 start, float3 end, float thickness, Color color) {
	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4_from3(start, thickness),
		.b = make4_from3(end, thickness),
		.color = color_pack_uint32(color),
	};
}

void draw3d_arrow(Arena *arena, float3 start, float3 end, float thickness, Color color,
	float4x4 view, float4x4 projeciton, float viewport_width) {
	float3 direction = sub3(end, start);
	float total_world_length = len3(direction);
	if (total_world_length < EPSILON)
		return;

	float3 dir_norm = scale3(direction, 1.0f / total_world_length);

	float4x4 vp = mul4x4(projeciton, view);
	float w = vp.elements[3] * end.x + vp.elements[7] * end.y + vp.elements[11] * end.z + vp.elements[15] * 1.0f;
	float desired_pixel_length = thickness * 5.0f;

	float world_head_length = (2.0f * w * desired_pixel_length) / (projeciton.elements[0] * viewport_width);
	if (world_head_length > total_world_length * 0.5f)
		world_head_length = total_world_length * 0.5f;

	float3 shaft_end = sub3(end, scale3(dir_norm, world_head_length));

	draw3d_line(arena, start, shaft_end, thickness, color);
	*arena_push_count(arena, LineVertex3D, 1) = (LineVertex3D){
		.a = make4_from3(shaft_end, thickness * 4.0f),
		.b = make4_from3(end, 0.0f),
		.color = color_pack_uint32(color),
	};
}

void draw3d_triangle_outline(Arena *arena, Triangle3 t, float thickness, Color color) {
	draw3d_line(arena, t.a, t.b, thickness, color);
	draw3d_line(arena, t.b, t.c, thickness, color);
	draw3d_line(arena, t.c, t.a, thickness, color);
}

void draw3d_quad_outline(Arena *arena, Plane plane, float width, float height, float thickness, Color color) {
	float3 right = { 0 }, up = { 0 };
	float dot = dot3(plane.normal, unit3(UP));
	if (fabsf(dot) >= 0.99f) {
		right.x = dot > 0 ? 1.0f : -1.0f;
		up.z = -1.0f;
	} else {
		right = norm3(cross3(plane.normal, (float3){ 0.0f, 1.0f, 0.0f }));
		up = norm3(cross3(plane.normal, right));
	}

	float3 center = scale3(plane.normal, plane.distance);
	float3 h = scale3(right, width * 0.5f);
	float3 v = scale3(up, height * 0.5f);

	float3 corners[] = {
		add3(sub3(center, h), v),
		add3(add3(center, h), v),
		sub3(add3(center, h), v),
		sub3(sub3(center, h), v),
	};

	for (uint32_t index = 0; index < countof(corners); ++index)
		draw3d_line(arena, corners[index], corners[(index + 1) % countof(corners)], thickness, color);
}

void draw3d_shape_outline(Arena *arena, Shape3 *shape, float3 offset, float thickness) {
	switch (shape->kind) {
		case SHAPE_KIND_AABB3:
			draw3d_aabb_outline(arena, aabb3_move(shape->as.aabb3, offset), thickness, WHITE);
			break;
		case SHAPE_KIND_SPHERE: {
			float3 c = add3(shape->as.sphere.center, offset);
			float r = shape->as.sphere.radius;
			uint8_t segments = 32;

			draw3d_sphere_outline(arena, c, r, segments, thickness, WHITE);
		} break;
		case SHAPE_KIND_CAPSULE3: {
			float r = shape->as.capsule.radius;
			float3 centers[] = {
				add3(shape->as.capsule.a, offset),
				add3(shape->as.capsule.b, offset),
			};
			draw3d_capsule_outline(arena, centers[0], centers[1], r, 32, thickness, WHITE);
		} break;
			break;
		case SHAPE_KIND_PLANE:
			break;
		case SHAPE_KIND_CONVEX_POLYGON:
			break;
		case SHAPE_KIND_MAX:
			break;
	}
}
