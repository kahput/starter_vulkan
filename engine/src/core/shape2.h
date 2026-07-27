#pragma once

#include "cmath.h"

typedef struct {
	float2 a, b, c;
} Triangle2;

typedef struct {
	float x, y, width, height;
} Rectangle;
#define rect(x, y, w, h) \
	(Rectangle) { x, y, w, h }

static inline bool rect_contains(Rectangle rect, float x, float y) { return x > rect.x && x < rect.x + rect.width && y > rect.y && y < rect.y + rect.height; }
static inline bool rect_contains_point(Rectangle rect, float2 position) { return rect_contains(rect, position.x, position.y); }
static inline Rectangle rect_from_center(float2 center, float2 half_extent) { return (Rectangle){ center.x - half_extent.x, center.y - half_extent.y, half_extent.x * 2.0f, half_extent.y * 2.0f }; }
