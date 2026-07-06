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

static inline Rectangle rect_from_dimensions(float width, float height) { return (Rectangle){ 0, 0, width, height }; }
static inline bool rect_contains(Rectangle rect, float x, float y) { return x > rect.x && x < rect.x + rect.width && y > rect.y && y < rect.y + rect.height; }
static inline bool rect_contains_float2(Rectangle rect, float2 position) { return rect_contains(rect, position.x, position.y); }
