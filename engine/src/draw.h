#pragma once

#include "core/arena.h"

#include "gfx/font.h"
#include "gfx/gfx_types.h"

typedef struct {
	float2 position, uv;
	float4 radii;
	float2 size;
	uint32_t fill_color, border_color;
	float border_width;
	uint32_t imageid;
} QuadVertex2D;

typedef struct {
	float4 a, b; // xyz + thickness
	uint32_t color;
	float3 _pad0;
} LineVertex3D;

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

void draw2d_quad(Arena *arena, Rectangle dst, Rectangle src, Image2D *image, float2 origin, float rotation, float border_width, Color border_color, float4 radii, Color fill_color);
void draw2d_rect(Arena *arena, Rectangle rect, Color color);
void draw2d_rect_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, Color color);
void draw2d_rect_outline(Arena *arena, Rectangle rect, float thickness, Color color);

void draw2d_rect_rounded(Arena *arena, Rectangle rect, float4 radii, Color color);
void draw2d_rect_rounded_ex(Arena *arena, Rectangle rect, float2 origin, float rotation, float4 radii, Color color);

void draw2d_sprite_ex(Arena *arena, Rectangle src, Rectangle dst, Image2D *image, Color tint);
void draw2d_sprite(Arena *arena, float2 position, Image2D *image, Color tint);

void draw2d_point(Arena *arena, float2 position, float radius, Color color);
void draw2d_textf(Arena *arena, Font *font, float2 position, Color color, String8 format, ...);

void draw2d_line(Arena *arena, float2 start, float2 end, float thickness, Color color);
void draw2d_arrow(Arena *arena, float2 start, float2 end, float thickness, Color color);
void draw2d_triangle(Arena *arena, Triangle2 triangle, float thickness, Color color);

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
