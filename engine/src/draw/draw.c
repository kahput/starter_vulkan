#include "draw.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"

#include "core/geom.h"
#include "core/geom_types.h"
#include "gfx.h"
#include "gfx/gfx_types.h"

#define DRAW_QUAD_INSTANCE_MAX 8192
#define DRAW_LINE_INSTANCE_MAX 8192

DRAW_List *CURRENT_DRAWLIST = 0;

DRAW_List *drawlist_make(Arena *arena, RES_ID default_quad2d, RES_ID default_line3d) {
	bool ok = arena != 0;
	if (ok) {
		CURRENT_DRAWLIST = arena_push_count(arena, DRAW_List, 1);
		CURRENT_DRAWLIST->arena = arena;

		CURRENT_DRAWLIST->line3d.instances = (Arena){
			.base = arena_push_count(arena, DRAW_LineInstance3D, DRAW_LINE_INSTANCE_MAX),
			.capacity = sizeof(DRAW_LineInstance3D) * DRAW_LINE_INSTANCE_MAX,
		};
		CURRENT_DRAWLIST->quad2d.instances = (Arena){
			.base = arena_push_count(arena, DRAW_QuadInstance3D, DRAW_QUAD_INSTANCE_MAX),
			.capacity = sizeof(DRAW_QuadInstance3D) * DRAW_QUAD_INSTANCE_MAX,
		};

		CURRENT_DRAWLIST->quad2d.default_shader = default_quad2d;
		CURRENT_DRAWLIST->line3d.default_shader = default_line3d;
	}

	return CURRENT_DRAWLIST;
}

static void draw_push_shader(DRAW_Channel *ch, RES_ID s) {
	ASSERT(ch->shader_stack_count < DRAW_SHADER_STACK_MAX);
	ch->shader_stack[ch->shader_stack_count++] = s;
}
static void draw_pop_shader(DRAW_Channel *ch) {
	ASSERT(ch->shader_stack_count > 0);
	ch->shader_stack_count--;
}

void draw2d_push_shader(RES_ID s) { draw_push_shader(&CURRENT_DRAWLIST->quad2d, s); }
void draw2d_pop_shader(void) { draw_pop_shader(&CURRENT_DRAWLIST->quad2d); }

static void *draw__channel_push(Arena *arena, DRAW_Channel *ch, uint64_t size) {
	RES_ID current = ch->shader_stack_count
		? ch->shader_stack[ch->shader_stack_count - 1]
		: ch->default_shader;

	DRAW_Batch *last = ch->last_batch;
	if (last == 0 || last->shader != current) {
		DRAW_Batch *new_entry = arena_push_count(arena, DRAW_Batch, 1);
		new_entry->shader = current;
		new_entry->instance_offset = ch->instances.offset;
		new_entry->instance_count = 0;

		if (last == 0)
			ch->first_batch = new_entry;
		else
			last->next = new_entry;

		last = ch->last_batch = new_entry;
	}

	void *slot = arena_push(&ch->instances, size, 1, false);
	last->instance_count++;
	return slot;
}

#define draw__channel_push_instance(a, ch, T) draw__channel_push((a), (ch), sizeof(T))

void draw2d_quad(Rectangle rect, DRAW_QuadStyle style) {
	bool ok = CURRENT_DRAWLIST;
	if (ok) {
		float2 position = { rect.x, rect.y };
		float2 size = { rect.width, rect.height };

		float2 uv0 = splat2(0.0f);
		float2 uv1 = splat2(1.0f);
		if (style.image && style.uv.width != 0.0f && style.uv.height != 0.0f) {
			uv0 = make2(style.uv.x / style.image->width, style.uv.y / style.image->height);
			uv1 = make2((style.uv.x + style.uv.width) / style.image->width, (style.uv.y + style.uv.height) / style.image->height);
		}

		uint32_t imageid = style.image ? style.image->handle->imageid : 0;

		float rad = style.rotation * DEG2RAD;
		DRAW_QuadInstance3D quad = {
			.position = position,
			.size = size,
			.radii = style.radii,
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

		memory_copy(draw__channel_push_instance(CURRENT_DRAWLIST->arena, &CURRENT_DRAWLIST->quad2d, DRAW_QuadInstance3D), &quad, sizeof(quad));
	}
}

void draw2d_text(Font *font, float2 position, Color color, string8 text) {
	bool ok = CURRENT_DRAWLIST && font;
	if (ok) {
		float y_offset = font->greatest_top_y;
		float x_offset = 0.0f;
		for (uint32_t index = 0; index < text.length; ++index) {
			uint8_t c = text.bytes[index];
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

			draw2d_quad(rect, (DRAW_QuadStyle){ .image = &font->atlas, .fill_color = color, .uv = glyph->src });
			x_offset += glyph->advance_x;
		}
	}
}

void draw2d_textf(Font *font, float2 position, Color color, const char *format, ...) {
	ArenaTemp scratch = arena_scratch_begin(0);

	va_list args;
	va_start(args, format);
	string8 text = fmtv8(scratch.arena, format, args);
	draw2d_text(font, position, color, text);
	va_end(args);

	arena_scratch_end(scratch);
}

void draw2d_line(float2 start, float2 end, float thickness, Color color) {
	float2 vec = sub2(end, start);

	float len_sq = lensq2(vec);
	if (len_sq <= EPSILON * EPSILON)
		return;

	float len = sqrtf(len_sq);

	float2 center = scale2(add2(start, end), 0.5f);
	float2 half_extent = { .x = len * 0.5f, .y = thickness * 0.5f };

	float rot_rad = atan2f(vec.y, vec.x);
	Rectangle rect = rect_from_center(center, half_extent);

	draw2d_quad(rect,
		(DRAW_QuadStyle){
		  .fill_color = color,
		  .origin = half_extent,
		  .rotation = rot_rad * RAD2DEG,
		});
}

void draw2d_dashed(float2 start, float2 end, float thickness, float segment_length, float gap_length, Color color) {
	float2 diff = sub2(end, start);
	float len = len2(diff);
	float step = segment_length + gap_length;

	bool ok = len > EPSILON && segment_length > 0.0f && step > 0.0f;
	if (ok) {
		float2 direction = scale2(diff, 1.0f / len);

		for (float t = 0.0f; t < len; t += step) {
			float2 at = add2(start, scale2(direction, t));
			float2 to = add2(start, scale2(direction, minf(t + segment_length, len)));

			draw2d_line(at, to, thickness, color);
		}
	}
}

void draw2d_arrow(float2 origin, float2 delta, float thicknes, float head_lengh, Color color) {
	if (lensq2(delta) <= EPSILON * EPSILON) return;
	float2 end = add2(origin, delta);

	float2 tangent = norm2(make2(-delta.y, delta.x));
	float2 right = rotate2(tangent, 45.0f * DEG2RAD);
	float2 left = rotate2(right, 90.0f * DEG2RAD);

	draw2d_line(origin, end, thicknes, color);
	draw2d_line(end, add2(end, scale2(right, head_lengh)), thicknes, color);
	draw2d_line(end, add2(end, scale2(left, head_lengh)), thicknes, color);
}

void draw2d_triangle_outline(Triangle2 t, float thickness, Color color) {
	draw2d_line(t.a, t.b, thickness, color);
	draw2d_line(t.b, t.c, thickness, color);
	draw2d_line(t.c, t.a, thickness, color);
}

void draw2d_circle_outline(float2 center, float radius, float thickness, Color color) {
	draw2d_quad(rect_from_center(center, splat2(radius)),
		(DRAW_QuadStyle){
		  .border_color = color,
		  .border_width = thickness,
		  .radii = splat4(radius),
		});
}

void draw3d_arc_basis(float3 center, float2 radius, uint8_t segments, float3 axis_x, float3 axis_y, float angle_start, float angle_span, float thickness, Color color) {
	bool ok = CURRENT_DRAWLIST && segments;

	if (ok) {
		for (uint32_t i = 0; i < segments; ++i) {
			float a = angle_start + (((float)i / segments) * angle_span);
			float an = angle_start + (((float)(i + 1) / segments) * angle_span);
			float ca = cosf(a), sa = sinf(a);
			float can = cosf(an), san = sinf(an);

			float3 start = add3(center, add3(scale3(axis_x, ca * radius.x), scale3(axis_y, sa * radius.y)));
			float3 end = add3(center, add3(scale3(axis_x, can * radius.x), scale3(axis_y, san * radius.y)));

			draw3d_line(start, end, thickness, color);
		}
	}
}

void draw3d_arc(float3 center, float2 radius, uint8_t segments, float3 normal, float angle_start, float angle_span, float thickness, Color color) {
	float3 right, up;
	normal = orthobasis3(normal, &right, &up);
	draw3d_arc_basis(center, radius, segments, right, up, angle_start, angle_span, thickness, color);
}

void draw3d_sphere_outline(float3 center, float radius, uint8_t segments, float thickness, Color color) {
	draw3d_arc(center, splat2(radius), segments, unit3(UP), 0, TAU, thickness, color);
	draw3d_arc(center, splat2(radius), segments, unit3(RIGHT), 0, TAU, thickness, color);
	draw3d_arc(center, splat2(radius), segments, unit3(FORWARD), 0, TAU, thickness, color);
}

void draw3d_ellipsoid_outline(float3 center, float3 r, uint8_t segments, float thickness, Color color) {
	draw3d_arc(center, make2(r.x, r.z), segments, unit3(UP), 0, TAU, thickness, color);
	draw3d_arc(center, make2(r.z, r.y), segments, unit3(RIGHT), 0, TAU, thickness, color);
	draw3d_arc(center, make2(r.x, r.y), segments, unit3(FORWARD), 0, TAU, thickness, color);
}

void draw3d_capsule_outline(float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color) {
	float3 direction = sub3(b, a);
	if (lensq3(direction) < EPSILON) return;

	float3 right, up;
	direction = orthobasis3(direction, &right, &up);

	uint32_t packed_color = color_pack_uint32(color);
	DRAW_LineInstance3D spine[] = {
		{ make4_from3(add3(a, scale3(right, -radius)), thickness), make4_from3(add3(b, scale3(right, -radius)), thickness), packed_color, splat3(0.0f) },
		{ make4_from3(add3(a, scale3(right, radius)), thickness), make4_from3(add3(b, scale3(right, radius)), thickness), packed_color, splat3(0.0f) },

		{ make4_from3(add3(a, scale3(up, -radius)), thickness), make4_from3(add3(b, scale3(up, -radius)), thickness), packed_color, splat3(0.0f) },
		{ make4_from3(add3(a, scale3(up, radius)), thickness), make4_from3(add3(b, scale3(up, radius)), thickness), packed_color, splat3(0.0f) },
	};
	memory_copy_array(arena_push_count(&CURRENT_DRAWLIST->line3d.instances, DRAW_LineInstance3D, countof(spine)), spine);

	for (uint32_t end = 0; end < 2; ++end) {
		float3 c = end == 0 ? a : b;
		float signed_r = (end == 0) ? -radius : radius;

		draw3d_arc_basis(c, splat2(radius), segments, right, up, 0, TAU, thickness, color);
		draw3d_arc_basis(c, splat2(signed_r), segments, direction, up, -PIf * 0.5f, PIf, thickness, color);
		draw3d_arc_basis(c, splat2(signed_r), segments, right, direction, 0, PIf, thickness, color);
	}
}

void draw3d_aabb_outline(AABB3 aabb3, float thickness, Color color) {
	float3 min = aabb3.min;
	float3 max = aabb3.max;
	float3 bounding_box_size = sub3(max, min);

	DRAW_LineInstance3D outline[] = {
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

	DRAW_LineInstance3D *points = arena_push_count(&CURRENT_DRAWLIST->line3d.instances, DRAW_LineInstance3D, countof(outline));
	memory_copy_array(points, outline);
}

void draw3d_line(float3 start, float3 end, float thickness, Color color) {
	*arena_push_count(&CURRENT_DRAWLIST->line3d.instances, DRAW_LineInstance3D, 1) = (DRAW_LineInstance3D){
		.a = make4_from3(start, thickness),
		.b = make4_from3(end, thickness),
		.color = color_pack_uint32(color),
	};
}

void draw3d_arrow(float3 start, float3 end, float thickness, Color color,
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

	draw3d_line(start, shaft_end, thickness, color);
	*arena_push_count(&CURRENT_DRAWLIST->line3d.instances, DRAW_LineInstance3D, 1) = (DRAW_LineInstance3D){
		.a = make4_from3(shaft_end, thickness * 4.0f),
		.b = make4_from3(end, 0.0f),
		.color = color_pack_uint32(color),
	};
}

void draw3d_triangle_outline(Triangle3 t, float thickness, Color color) {
	draw3d_line(t.a, t.b, thickness, color);
	draw3d_line(t.b, t.c, thickness, color);
	draw3d_line(t.c, t.a, thickness, color);
}

void draw3d_quad_outline(Plane plane, float width, float height, float thickness, Color color) {
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
		draw3d_line(corners[index], corners[(index + 1) % countof(corners)], thickness, color);
}

void draw3d_shape_outline(Shape3 *shape, float3 offset, float thickness) {
	switch (shape->kind) {
		case SHAPE_KIND_AABB3:
			draw3d_aabb_outline(aabb3_move(shape->as.aabb3, offset), thickness, WHITE);
			break;
		case SHAPE_KIND_SPHERE: {
			float3 c = add3(shape->as.sphere.center, offset);
			float r = shape->as.sphere.radius;
			uint8_t segments = 32;

			draw3d_sphere_outline(c, r, segments, thickness, WHITE);
		} break;
		case SHAPE_KIND_CAPSULE3: {
			float r = shape->as.capsule.radius;
			float3 centers[] = {
				add3(shape->as.capsule.a, offset),
				add3(shape->as.capsule.b, offset),
			};
			draw3d_capsule_outline(centers[0], centers[1], r, 32, thickness, WHITE);
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
