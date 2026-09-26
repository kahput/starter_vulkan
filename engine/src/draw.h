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
} DRAW_QuadInstance3D;

typedef struct {
	float4 a, b; // xyz + thickness
	uint32_t color;
	float3 _pad0;
} DRAW_LineInstance3D;

typedef struct {
	float3 position;
	float _pad0;
	float3 normal;
	float _pad1;
	float2 uv;
	float4 tangent;
} DRAW_Vertex3D;

typedef struct {
	uint4 bone_ids;
	float4 weights;
} DRAW_SkinningVertex3D;

typedef struct {
	Color fill_color;

	Color border_color;
	float border_width;

	float4 radii;

	Image2D *image;
	Rectangle uv;

	float2 origin;
	float rotation;

	uint32_t flags;
} DRAW_QuadStyle;

typedef struct {
	Arena quad2d[1];
	Arena quad3d[1], line3d[1];
} DRAW_List;

// Pushes list to arena and sets thread-local context to list.
DRAW_List *drawlist_make(Arena *arena);

void draw2d_quad(Rectangle rect, DRAW_QuadStyle style);
INLINE void draw2d_rect(Rectangle rect, Color color) { draw2d_quad(rect, (DRAW_QuadStyle){ .fill_color = color }); }
INLINE void draw2d_rect_outline(Rectangle rect, float thickness, Color color) { draw2d_quad(rect, (DRAW_QuadStyle){ .border_color = color, .border_width = thickness }); }
INLINE void draw2d_rect_rounded(Rectangle rect, float4 radii, Color color) { draw2d_quad(rect, (DRAW_QuadStyle){ .radii = radii, .fill_color = color }); }
INLINE void draw2d_sprite(float2 position, Image2D *image, Color tint) { draw2d_quad(rect(position.x, position.y, image->width, image->height), (DRAW_QuadStyle){ .image = image, .fill_color = tint }); }
INLINE void draw2d_circle(float2 position, float radius, Color color) { draw2d_quad(rect(position.x - radius, position.y - radius, radius * 2.0, radius * 2.0), (DRAW_QuadStyle){ .fill_color = color, .origin = splat2(radius), .radii = splat4(radius) }); }

void draw2d_text(Font *font, float2 position, Color color, string8 text);
void draw2d_textf(Font *font, float2 position, Color color, const char *format, ...);

void draw2d_line(float2 start, float2 end, float thickness, Color color);
void draw2d_dashed(float2 start, float2 end, float thickness, float segment_length, float gap_length, Color color);
void draw2d_arrow(float2 origin, float2 delta, float thickness, float head_lengh, Color color);

void draw2d_triangle_outline(Triangle2 triangle, float thickness, Color color);
void draw2d_circle_outline(float2 center, float radius, float thickness, Color color);

void draw3d_line(float3 start, float3 end, float thickness, Color color);
void draw3d_arrow(float3 start, float3 end, float thickness, Color color,
	float4x4 view, float4x4 projeciton, float viewport_width);
void draw3d_arc(float3 center, float2 radius, uint8_t segments, float3 normal, float angle_start, float angle_span, float thickness, Color color);
void draw3d_arc_basis(float3 center, float2 radius, uint8_t segments, float3 axis_x, float3 axis_y, float angle_start, float angle_span, float thickness, Color color);

void draw3d_sphere_outline(float3 center, float radius, uint8_t segments, float thickness, Color color);
void draw3d_ellipsoid_outline(float3 center, float3 radius, uint8_t segments, float thickness, Color color);
void draw3d_capsule_outline(float3 a, float3 b, float radius, uint8_t segments, float thickness, Color color);

void draw3d_aabb_outline(AABB3 aabb3, float thickness, Color color);
void draw3d_triangle_outline(Triangle3 t, float thickness, Color color);

void draw3d_quad_outline(Plane plane, float width, float height, float thickness, Color color);
void draw3d_shape_outline(Shape3 *shape, float3 offset, float thickness);
