#pragma once

#include "common.h"
#include "draw/font.h"
#include "draw/camera.h"
#include "gfx/gfx_types.h"

#include "core/geom.h"
#include "core/arena.h"

typedef struct {
	float2 position, size;
	float4 radii;
	float2 uvs[4];

	uint32_t imageid, flags;
	uint32_t fill_color, border_color;

	float2 origin, rotation;
	float border_width;
	float3 _pad0;
} DRAW_Quad2D;

typedef struct {
	float4 a, b; // xyz + thickness
	uint32_t color;
	float3 _pad0;
} DRAW_Line3D;

typedef enum {
	DRAW_PASS_SHADOW,
	DRAW_PASS_OPAQUE,
	DRAW_PASS_SKYBOX,
	DRAW_PASS_TRANSPARENT,
	DRAW_PASS_DEBUG_OVERLAY,
	DRAW_PASS_2D,

	DRAW_PASS_MAX,
} DRAW_PassKind;

/*
typedef struct {
	float3 position;
	float border_width;

	float2 uv, size;
	float4 radii;

	uint32_t imageid, flags;
	uint32_t fill_color, border_color;
} Quad3D;
*/

typedef struct {
	float3 position;
	float _pad0;
	float3 normal;
	float _pad1;
	float2 uv;
	float4 tangent;
} Vertex3D;

typedef struct {
	uint4 bone_ids;
	float4 weights;
} SkinningVertex3D;

typedef struct {
	Color fill_color;

	Color border_color;
	float border_width;

	float4 corner_radii;

	Image2D *image;
	Rectangle uv;

	float2 origin;
	float rotation;
} DRAW_QuadStyle;

void draw2d_quad(Arena *arena, Rectangle rect, DRAW_QuadStyle style);
void draw2d_triangle(Arena *arena, Triangle2 t);

ENSURE_INLINE void draw2d_rect(Arena *arena, Rectangle rect, Color color) { draw2d_quad(arena, rect, (DRAW_QuadStyle){ .fill_color = color }); }
ENSURE_INLINE void draw2d_rect_outline(Arena *arena, Rectangle rect, float thickness, Color color) { draw2d_quad(arena, rect, (DRAW_QuadStyle){ .border_color = color, .border_width = thickness }); }
ENSURE_INLINE void draw2d_rect_rounded(Arena *arena, Rectangle rect, float4 radii, Color color) { draw2d_quad(arena, rect, (DRAW_QuadStyle){ .corner_radii = radii, .fill_color = color }); }
ENSURE_INLINE void draw2d_sprite(Arena *arena, float2 position, Image2D *image, Color tint) { draw2d_quad(arena, rect(position.x, position.y, image->width, image->height), (DRAW_QuadStyle){ .image = image, .fill_color = tint }); }
ENSURE_INLINE void draw2d_circle(Arena *arena, float2 position, float radius, Color color) { draw2d_quad(arena, rect(position.x, position.y, radius * 2.0, radius * 2.0), (DRAW_QuadStyle){ .fill_color = color, .origin = splat2(radius), .corner_radii = splat4(radius) }); }

void draw2d_textf(Arena *arena, Font *font, float2 position, Color color, String8 format, ...);
void draw2d_line(Arena *arena, float2 start, float2 end, float thickness, Color color);

void draw2d_triangle_outline(Arena *arena, Triangle2 triangle, float thickness, Color color);
void draw2d_arrow(Arena *arena, float2 start, float2 end, float thickness, Color color);

void draw3d_line(Arena *arena, float3 start, float3 end, float thickness, Color color);

void draw3d_arrow(Arena *arena, float3 start, float3 end, float thickness, Color color,
	float4x4 view, float4x4 projeciton, float viewport_width);
void draw3d_arc(Arena *arena, float3 center, float radius, uint8_t segments, Side plane, float angle_span, float thickness, Color color);

void draw3d_sphere_outline(Arena *arena, float3 center, float radius, uint8_t segments, float thickness, Color color);
void draw3d_capsule_outline(Arena *arena, float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color);

void draw3d_aabb_outline(Arena *arena, AABB3 aabb3, float thickness, Color color);
void draw3d_triangle_outline(Arena *arena, Triangle3 t, float thickness, Color color);

void draw3d_quad_outline(Arena *arena, Plane plane, float width, float height, float thickness, Color color);
void draw3d_shape_outline(Arena *arena, Shape3 *shape, float3 offset, float thickness);

// stateful
void draw3d_sphere(Arena *arena, float3 center, float radius, Color color);
void draw3d_ellipsoid(Arena *arena, float3 center, float3 radius, Color color);
