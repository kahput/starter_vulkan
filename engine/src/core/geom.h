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
	float x, y, width, height;
} Rectangle;
#define rect(x, y, w, h) \
	(Rectangle) { x, y, w, h }

static inline Rectangle rect_from_dimensions(float width, float height) { return (Rectangle){ 0, 0, width, height }; }
static inline bool rect_contains(Rectangle rect, float x, float y) { return x > rect.x && x < rect.x + rect.width && y > rect.y && y < rect.y + rect.height; }
static inline bool rect_contains_float2(Rectangle rect, float2 position) { return rect_contains(rect, position.x, position.y); }

typedef struct {
	float3 a, b;
} Segment3;

typedef struct {
	float3 a, b, c;
} Triangle3;

typedef struct {
	float2 a, b, c;
} Triangle2;

typedef struct {
	float3 min, max;
} AABB3;

typedef struct {
	float2 min, max;
} AABB2;

static inline AABB2 aabb2_from_center(float2 center, float2 half_extent) { return (AABB2){ .min = float2_subtract(center, half_extent), .max = float2_add(center, half_extent) }; }

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
static inline Plane plane_from_triangle(Triangle3 t) {
	Plane result = { 0 };

	result.normal = float3_normalize_safe(float3_cross(float3_subtract(t.b, t.a), float3_subtract(t.c, t.a)), EPSILON);
	result.distance = float3_dot(result.normal, t.a);

	return result;
}

typedef struct {
	float3 a, b;
	float radius;
} Capsule3;

static inline Capsule3 capsule_from_center(float3 center, float3 up, float height, float radius) {
	float actual_height = fmaxf(height, radius * 2.0f);

	float3 offset = float3_scale(up, (actual_height * 0.5f) - radius);
	return (Capsule3){ float3_subtract(center, offset), float3_add(center, offset), radius };
}

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
		Capsule3 capsule;
		Plane plane;
	} as;
} Shape;

static inline Shape shape_sphere(float3 center, float radius) { return (Shape){ .kind = SHAPE_KIND_SPHERE, .as.sphere = { .center = center, .radius = radius } }; }
static inline Shape shape_capsule(float3 center, float height, float radius) { return (Shape){ .kind = SHAPE_KIND_CAPSULE, .as.capsule = capsule_from_center(center, FLOAT3_Y, height, radius) }; }
static inline Shape shape_from_aabb3(AABB3 a) { return (Shape){ .kind = SHAPE_KIND_AABB3, .as.aabb3 = a }; }

Raycast3Result raycast_plane(float3 ro, float3 rd, float3 po, float3 pn);
Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 extent);

Raycast3Result sphere_sweep_triangle3(float3 origin, float radius, float3 direction, float max_distance, Triangle3 triangle);

float segment3_squared_distance(Segment3 segment, float3 point);

float3 plane_closest_point(Plane p, float3 to);
float3 segment3_closest_point(Segment3 segment, float3 to);
float3 triangle3_closest_point(Triangle3 t, float3 to);
float3 aabb3_closest_point(AABB3 a, float3 to);

bool triangle3_contains_point(Triangle3 triangle, float3 point);

bool lowest_root(float a, float b, float c, float max_r, float *root);

#endif /* GEOM_H_ */
