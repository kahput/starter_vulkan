#include "draw.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"

#include "core/geom.h"
#include "gfx.h"
#include "gfx/gfx_types.h"

void draw2d_quad(Arena *arena, Rectangle rect, DRAW_QuadStyle style) {
	bool ok = arena;
	if (ok) {
		float2 position = { rect.x, rect.y };
		float2 size = { rect.width, rect.height };

		float2 min = sub2(position, style.origin), max = add2(min, size);

		float2 uv0 = splat2(0.0f);
		float2 uv1 = splat2(1.0f);
		if (style.image && style.uv.width != 0.0f && style.uv.height != 0.0f) {
			uv0 = make2(style.uv.x / style.image->width, style.uv.y / style.image->height);
			uv1 = make2((style.uv.x + style.uv.width) / style.image->width, (style.uv.y + style.uv.height) / style.image->height);
		}

		uint32_t imageid = style.image ? style.image->handle->imageid : 0;

		float rad = style.rotation * DEG2RAD;
		DRAW_Quad2D quad = {
			.position = position,
			.size = size,
			.radii = style.corner_radii,
			.rotation = { cosf(rad), sinf(rad) },
			.uvs = {
			  uv0,
			  { uv1.x, uv0.y },
			  { uv0.x, uv1.y },
			  uv1,
			},
			.origin = style.origin,
			.imageid = imageid,
			.flags = 0,
			.fill_color = color_pack_uint32(style.fill_color),
			.border_color = color_pack_uint32(style.border_color),
			.border_width = style.border_width,
		};

		memory_copy(arena_push_count(arena, DRAW_Quad2D, 1), &quad, sizeof(quad));
	}
}

void draw2d_text(Arena *arena, Font *font, float2 position, Color color, String8 text) {
	bool ok = arena && font;
	if (ok) {
		float y_offset = font->greatest_top_y;
		float x_offset = 0.0f;
		for (uint32_t index = 0; index < text.length; ++index) {
			uint8_t c = text.text[index];
			if (c == '\n') {
				x_offset = 0.0f;
				y_offset += font->greatest_bottom_y + font->greatest_top_y;
			}

			Glyph *glyph = &font->glyphs[c];

			Rectangle rect = {
				.x = position.x + x_offset + (glyph->bearing.x),
				.y = position.y + y_offset + (glyph->bearing.y),
				.width = glyph->src.width,
				.height = glyph->src.height,
			};

			draw2d_quad(arena, rect, (DRAW_QuadStyle){ .image = &font->atlas, .fill_color = color, .uv = glyph->src });
			x_offset += glyph->advance_x;
		}
	}
}

void draw2d_textf(Arena *arena, Font *font, float2 position, Color color, String8 format, ...) {
	ArenaTemp scratch = arena_scratch_begin(arena);

	va_list args;
	va_start(args, format);
	String8 text = str8_push_format_list(scratch.arena, format, args);
	draw2d_text(arena, font, position, color, text);
	va_end(args);

	arena_scratch_end(scratch);
}

void draw2d_line(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	float2 vec = sub2(end, start);

	float len_sq = len2_sq(vec);
	if (len_sq <= EPSILON * EPSILON)
		return;

	float len = sqrtf(len_sq);

	float2 center = scale2(add2(start, end), 0.5f);
	float2 half_extent = { .x = len * 0.5f, .y = thickness * 0.5f };

	float rot_rad = atan2f(vec.y, vec.x);
	Rectangle rect = rect_from_center(center, half_extent);

	draw2d_quad(arena, rect,
		(DRAW_QuadStyle){
		  .fill_color = color,
		  .origin = half_extent,
		  .rotation = rot_rad * RAD2DEG,
		});
}

void draw2d_triangle_outline(Arena *arena, Triangle2 t, float thickness, Color color) {
	draw2d_line(arena, t.a, t.b, thickness, color);
	draw2d_line(arena, t.b, t.c, thickness, color);
	draw2d_line(arena, t.c, t.a, thickness, color);
}

void draw2d_arrow(Arena *arena, float2 start, float2 end, float thickness, Color color) {
	float2 shaft_end = add2(start, scale2(sub2(end, start), 0.75f));
	draw2d_line(arena, start, shaft_end, thickness, color);

	*arena_push_count(arena, DRAW_Line3D, 1) = (DRAW_Line3D){
		.a = make4(shaft_end.x, shaft_end.y, 0.0f, thickness * 4.0f),
		.b = make4(end.x, end.y, 0.0f, 0.0f),
		.color = color_pack_uint32(color),
	};
}

void draw3d_arc(Arena *arena, float3 center, float3 radius, uint8_t segments, Side plane, float angle_span, float thickness, Color color) {
	for (uint32_t i = 0; i < segments; ++i) {
		float a = ((float)i / segments) * angle_span;
		float an = ((float)(i + 1) / segments) * angle_span;
		float ca = cosf(a), sa = sinf(a);
		float can = cosf(an), san = sinf(an);

		float3 p0, p1;
		switch (plane) {
			case SIDE_BOTTOM:
			case SIDE_TOP:
				p0 = (float3){ center.x + ca * radius.x, center.y, center.z + sa * radius.z };
				p1 = (float3){ center.x + can * radius.x, center.y, center.z + san * radius.z };
				break;
			case SIDE_LEFT:
			case SIDE_RIGHT:
				p0 = (float3){ center.x, center.y + sa * radius.y, center.z + ca * radius.z };
				p1 = (float3){ center.x, center.y + san * radius.y, center.z + can * radius.z };
				break;
			case SIDE_BACK:
			case SIDE_FRONT:
				p0 = (float3){ center.x + ca * radius.x, center.y + sa * radius.y, center.z };
				p1 = (float3){ center.x + can * radius.x, center.y + san * radius.y, center.z };
				break;

			default:
				ASSERT(false);
				break;
		}

		draw3d_line(arena, p0, p1, thickness, color);
	}
}

void draw3d_sphere_outline(Arena *arena, float3 center, float radius, uint8_t segments, float thickness, Color color) {
	draw3d_arc(arena, center, splat3(radius), segments, SIDE_TOP, TAU, thickness, color);
	draw3d_arc(arena, center, splat3(radius), segments, SIDE_RIGHT, TAU, thickness, color);
	draw3d_arc(arena, center, splat3(radius), segments, SIDE_FRONT, TAU, thickness, color);
}

void draw3d_ellipsoid_outline(Arena *arena, float3 center, float3 radius, uint8_t segments, float thickness, Color color) {
	draw3d_arc(arena, center, radius, segments, SIDE_TOP, TAU, thickness, color);
	draw3d_arc(arena, center, radius, segments, SIDE_RIGHT, TAU, thickness, color);
	draw3d_arc(arena, center, radius, segments, SIDE_FRONT, TAU, thickness, color);
}

void draw3d_capsule_outline(Arena *arena, float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color) {
	DRAW_Line3D *spine_points = arena_push_count(arena, DRAW_Line3D, 8);
	DRAW_Line3D spine[] = {
		{ { a.x - radius, a.y, a.z, thickness }, { a.x - radius, b.y, a.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { a.x + radius, a.y, a.z, thickness }, { a.x + radius, b.y, a.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { a.x, a.y, a.z - radius, thickness }, { a.x, b.y, a.z - radius, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { a.x, a.y, a.z + radius, thickness }, { a.x, b.y, a.z + radius, thickness }, color_pack_uint32(color), splat3(0.0f) },
	};
	memory_copy_array(spine_points, spine);

	for (uint32_t end = 0; end < 2; ++end) {
		float3 c = end == 0 ? a : b;
		float signed_r = (end == 0) ? -radius : radius;

		draw3d_arc(arena, c, splat3(radius), segments, SIDE_TOP, TAU, thickness, color);
		draw3d_arc(arena, c, splat3(signed_r), segments, SIDE_RIGHT, PIf, thickness, color);
		draw3d_arc(arena, c, splat3(signed_r), segments, SIDE_FRONT, PIf, thickness, color);
	}
}

void draw3d_aabb_outline(Arena *arena, AABB3 aabb3, float thickness, Color color) {
	float3 min = aabb3.min;
	float3 max = aabb3.max;
	float3 bounding_box_size = sub3(max, min);

	DRAW_Line3D outline[] = {
		{ { min.x, min.y, min.z, thickness }, { min.x, max.y, min.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { min.x, min.y, max.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { max.x, min.y, min.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { max.x, min.y, max.z, thickness }, { max.x, max.y, max.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { min.x, min.y, min.z, thickness }, { min.x, min.y, max.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { min.x, min.y, min.z, thickness }, { max.x, min.y, min.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { max.x, min.y, max.z, thickness }, { max.x, min.y, min.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { max.x, min.y, max.z, thickness }, { min.x, min.y, max.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { min.x, max.y, min.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { min.x, max.y, min.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { max.x, max.y, max.z, thickness }, { max.x, max.y, min.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
		{ { max.x, max.y, max.z, thickness }, { min.x, max.y, max.z, thickness }, color_pack_uint32(color), splat3(0.0f) },
	};

	DRAW_Line3D *points = arena_push_count(arena, DRAW_Line3D, countof(outline));
	memory_copy_array(points, outline);
}

void draw3d_line(Arena *arena, float3 start, float3 end, float thickness, Color color) {
	*arena_push_count(arena, DRAW_Line3D, 1) = (DRAW_Line3D){
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
	*arena_push_count(arena, DRAW_Line3D, 1) = (DRAW_Line3D){
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

	float3 c = scale3(plane.normal, plane.distance);
	float3 h = scale3(right, width * 0.5f);
	float3 v = scale3(up, height * 0.5f);

	float3 corners[] = {
		add3(sub3(c, h), v),
		add3(add3(c, h), v),
		sub3(add3(c, h), v),
		sub3(sub3(c, h), v),
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
