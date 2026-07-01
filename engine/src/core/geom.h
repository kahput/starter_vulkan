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
	float3 origin, direction;
} Ray3;
static inline float3 ray_at(Ray3 r, float t) { return float3_add(r.origin, float3_scale(r.direction, t)); }

typedef struct {
	float3 start, end;
} Segment3;

typedef struct {
	float3 min, max;
} AABB3;
static inline AABB3 aabb3_from_center(float3 center, float3 half_extent) { return (AABB3){ .min = float3_subtract(center, half_extent), .max = float3_add(center, half_extent) }; }
static inline float3 aabb3_center(AABB3 a) { return float3_scale(float3_add(a.min, a.max), 0.5f); }
static inline float3 aabb3_extent(AABB3 a) { return float3_subtract(a.max, a.min); }
static inline float3 aabb3_half_extent(AABB3 a) { return float3_scale(aabb3_extent(a), 0.5f); }
static inline AABB3 aabb3_empty(void) { return (AABB3){ .min = float3_splat(FLOAT_MAX), .max = float3_splat(FLOAT_MIN) }; }
static inline void aabb3_expand(AABB3 *a, float3 point) {
	a->min = float3_min(a->min, point);
	a->max = float3_max(a->max, point);
}
static inline AABB3 aabb3_merge(AABB3 a, AABB3 b) { return (AABB3){ .min = float3_min(a.min, b.min), .max = float3_max(a.max, b.max) }; }
static inline AABB3 aabb3_move(AABB3 a, float3 delta) { return (AABB3){ .min = float3_add(a.min, delta), .max = float3_add(a.max, delta) }; }
static inline bool aabb3_overlap(AABB3 a, AABB3 b) {
	return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
		(a.min.y <= b.max.y && a.max.y >= b.min.y) &&
		(a.min.z <= b.max.z && a.max.z >= b.min.z);
}

typedef struct {
	float3 center;
	float radius;
} Sphere;
static inline Sphere sphere_from_aabb3(AABB3 a) { return (Sphere){ aabb3_center(a), float3_length(aabb3_half_extent(a)) }; }
static inline bool sphere_contains_point(Sphere s, float3 p) { return float3_length_sq(float3_subtract(p, s.center)) <= s.radius * s.radius; }

typedef struct {
	float3 normal;
	float distance;
} Plane;

static inline Plane plane_from_point_normal(float3 point, float3 normal) { return (Plane){ normal, float3_dot(point, normal) }; }
static inline float plane_signed_distance(Plane p, float3 point) { return float3_dot(p.normal, point) - p.distance; }
static inline float3 plane_project_point(Plane p, float3 point) { return float3_subtract(point, float3_scale(p.normal, plane_signed_distance(p, point))); }

typedef struct {
	float3 a, b;
	float radius;
} Capsule;

static inline Capsule capsule_from_center(float3 center, float3 up, float height, float radius) {
	float actual_height = fmaxf(height, radius * 2.0f);

	float3 offset = float3_scale(up, (actual_height * 0.5f) - radius);
	return (Capsule){ float3_subtract(center, offset), float3_add(center, offset), radius };
}

typedef struct {
	float3 a, b, c;
} Triangle3;

typedef enum {
	SHAPE_KIND_AABB3,
	SHAPE_KIND_SPHERE,
	SHAPE_KIND_CAPSULE,
	SHAPE_KIND_PLANE,
	/* SHAPE_KIND_CYLINDER, */
	/* SHAPE_KIND_CONVEX_POLYGON, */
	/* SHAPE_KIND_CONCAVE_POLYGON, */
	/* SHAPE_KIND_HEIGHTMAP, */
} ShapeKind;

typedef struct {
	ShapeKind kind;

	union {
		AABB3 aabb3;
		Sphere sphere;
		Capsule capsule;
		Plane plane;
	} as;
} Shape;

static inline Shape shape_sphere(float3 center, float radius) { return (Shape){ .kind = SHAPE_KIND_SPHERE, .as.sphere = { .center = center, .radius = radius } }; }
static inline Shape shape_capsule(float3 center, float height, float radius) { return (Shape){ .kind = SHAPE_KIND_CAPSULE, .as.capsule = capsule_from_center(center, FLOAT3_Y, height, radius) }; }
static inline Shape shape_from_aabb3(AABB3 a) { return (Shape){ .kind = SHAPE_KIND_AABB3, .as.aabb3 = a }; }

Raycast3Result raycast_plane(float3 ro, float3 rd, float3 po, float3 pn);
Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 extent);

float3 plane3_closest_point(Plane p, float3 to);
float3 segment3_closest_point(float3 start, float3 end, float3 to);
float3 triangle3_closest_point(Triangle3 t, float3 to);

bool triangle3_contains_point(Triangle3 triangle, float3 point);

#endif /* GEOM_H_ */
