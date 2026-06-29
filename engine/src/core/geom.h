#ifndef GEOM_H_
#define GEOM_H_

#include "cmath.h"

typedef struct {
	bool hit;
	float t;
	float3 normal, point;
} Raycast3Result;
static Raycast3Result RAY3_NO_HIT = { false, INFINITY, { 0, 0, 0 }, { 0, 0, 0 } };

typedef struct {
	float3 min, max;
} aabb3;
static inline aabb3 aabb3_from_center(float3 center, float3 half_extent) { return (aabb3){ .min = float3_subtract(center, half_extent), .max = float3_add(center, half_extent) }; }
static inline float3 aabb3_center(aabb3 a) { return float3_scale(float3_add(a.min, a.max), 0.5f); }
static inline float3 aabb3_extent(aabb3 a) { return float3_subtract(a.max, a.min); }
static inline float3 aabb3_half_extent(aabb3 a) { return float3_scale(aabb3_extent(a), 0.5f); }
static inline aabb3 aabb3_empty(void) { return (aabb3){ .min = float3_splat(FLOAT_MAX), .max = float3_splat(FLOAT_MIN) }; }
static inline void aabb3_expand(aabb3 *a, float3 point) {
	a->min = float3_min(a->min, point);
	a->max = float3_max(a->max, point);
}
static inline aabb3 aabb3_merge(aabb3 a, aabb3 b) { return (aabb3){ .min = float3_min(a.min, b.min), .max = float3_max(a.max, b.max) }; }

typedef enum {
	SHAPE_KIND_AABB3,
} ShapeKind;

typedef struct {
	ShapeKind kind;

	union {
		aabb3 aabb3;
	} as;
} Shape;

Raycast3Result raycast_plane(float3 ro, float3 rd, float3 po, float3 pn);
Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 extent);

float distance_point_plane_squared(float3 point, float3 po, float3 pn);
float distance_point_segment_squared(float3 point, float3 a, float3 b);
float3 closest_point_on_plane3(float3 point, float3 po, float3 pn);
float3 closest_point_on_segment3(float3 point, float3 a, float3 b);

#endif /* GEOM_H_ */
