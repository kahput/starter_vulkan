#ifndef GEOM_H_
#define GEOM_H_

#include "cmath.h"
#include "core/arena.h"

typedef struct {
	bool hit;
	float t;
	float3 normal, point;
} Raycast3Result;
static Raycast3Result RAY3_NO_HIT = { false, INFINITY, { 0, 0, 0 }, { 0, 0, 0 } };

typedef struct {
	float3 origin, direction;
} Ray3;
static inline float3 ray_at(Ray3 r, float t) { return add3(r.origin, scale3(r.direction, t)); }

typedef struct {
	float3 a, b;
} Segment3;

typedef struct {
	float3 a, b, c;
} Triangle3;
static inline Triangle3 triangle3_move(Triangle3 t, float3 offset) { return (Triangle3){ add3(offset, t.a), add3(offset, t.b), add3(offset, t.c) }; }

typedef struct {
	float3 min, max;
} AABB3;

static inline AABB3 aabb3_from_center(float3 center, float3 half_extent) { return (AABB3){ .min = sub3(center, half_extent), .max = add3(center, half_extent) }; }
static inline float3 aabb3_center(AABB3 a) { return scale3(add3(a.min, a.max), 0.5f); }
static inline float3 aabb3_extent(AABB3 a) { return sub3(a.max, a.min); }
static inline float3 aabb3_half_extent(AABB3 a) { return scale3(aabb3_extent(a), 0.5f); }
static inline AABB3 aabb3_empty(void) { return (AABB3){ .min = splat3(FLOAT_MAX), .max = splat3(FLOAT_MIN) }; }
static inline void aabb3_expand(AABB3 *a, float3 point) {
	a->min = less3(a->min, point);
	a->max = more3(a->max, point);
}
static inline AABB3 aabb3_merge(AABB3 a, AABB3 b) { return (AABB3){ .min = less3(a.min, b.min), .max = more3(a.max, b.max) }; }
static inline AABB3 aabb3_move(AABB3 a, float3 displacement) { return (AABB3){ .min = add3(a.min, displacement), .max = add3(a.max, displacement) }; }
static inline bool aabb3_overlap(AABB3 a, AABB3 b) {
	return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
		(a.min.y <= b.max.y && a.max.y >= b.min.y) &&
		(a.min.z <= b.max.z && a.max.z >= b.min.z);
}

typedef struct {
	float3 center;
	float radius;
} Sphere;
static inline Sphere sphere_from_aabb3(AABB3 a) { return (Sphere){ aabb3_center(a), length3(aabb3_half_extent(a)) }; }
static inline bool sphere_contains_point(Sphere s, float3 p) { return length3_sq(sub3(p, s.center)) <= s.radius * s.radius; }

typedef struct {
	float3 normal;
	float distance;
} Plane;

static inline Plane plane_from_point_normal(float3 point, float3 normal) { return (Plane){ normal, dot3(point, normal) }; }
static inline float3 plane_center(Plane p) { return scale3(p.normal, p.distance); }
static inline float plane_signed_distance(Plane p, float3 point) { return dot3(p.normal, point) - p.distance; }
static inline float3 plane_project_point(Plane p, float3 point) { return sub3(point, scale3(p.normal, plane_signed_distance(p, point))); }
static inline Plane plane_from_triangle(Triangle3 t) {
	Plane result = { 0 };

	result.normal = normalize3_safe(cross3(sub3(t.b, t.a), sub3(t.c, t.a)), EPSILON);
	result.distance = dot3(result.normal, t.a);

	return result;
}

typedef struct {
	float3 a, b;
	float radius;
} Capsule3;
static inline Capsule3 capsule3_move(Capsule3 c, float3 displacement) { return (Capsule3){ add3(c.a, displacement), add3(c.b, displacement), c.radius }; }

static inline Capsule3 capsule_from_center(float3 center, float3 up, float height, float radius) {
	float actual_height = fmaxf(height, radius * 2.0f);

	float3 offset = scale3(up, (actual_height * 0.5f) - radius);
	return (Capsule3){ sub3(center, offset), add3(center, offset), radius };
}

typedef struct {
	float3 *vertices;
	uint32_t vertex_count;
} ConvexPolygon3;

static inline ConvexPolygon3 convex3_from_triangle3(Triangle3 *triangle) {
	ConvexPolygon3 result = {
		.vertices = &triangle->a,
		.vertex_count = 3,
	};

	return result;
}

static inline ConvexPolygon3 convex3_from_aabb3(Arena *arena, AABB3 aabb) {
	ConvexPolygon3 result = {
		.vertices = arena_push_count(arena, float3, 8),
		.vertex_count = 8,
	};
	float3 min = aabb.min;
	float3 max = aabb.max;

	result.vertices[0] = (float3){ min.x, min.y, min.z };
	result.vertices[1] = (float3){ max.x, min.y, min.z };
	result.vertices[2] = (float3){ min.x, min.y, max.z };
	result.vertices[3] = (float3){ max.x, min.y, max.z };

	result.vertices[4] = (float3){ min.x, max.y, min.z };
	result.vertices[5] = (float3){ max.x, max.y, min.z };
	result.vertices[6] = (float3){ min.x, max.y, max.z };
	result.vertices[7] = (float3){ max.x, max.y, max.z };

	return result;
}

typedef enum {
	SHAPE_KIND_AABB3,
	SHAPE_KIND_SPHERE,
	SHAPE_KIND_CAPSULE3,
	SHAPE_KIND_PLANE,
	SHAPE_KIND_CONVEX_POLYGON,
	/* SHAPE_KIND_CYLINDER, */
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
		ConvexPolygon3 convex;
	} as;
} Shape3;

static inline Shape3 shape3_sphere(float3 center, float radius) { return (Shape3){ .kind = SHAPE_KIND_SPHERE, .as.sphere = { .center = center, .radius = radius } }; }
static inline Shape3 shape3_capsule(float3 center, float height, float radius) { return (Shape3){ .kind = SHAPE_KIND_CAPSULE3, .as.capsule = capsule_from_center(center, FLOAT3_Y, height, radius) }; }
static inline Shape3 shape3_convex_polygon(float3 *vertices, uint32_t vertex_count) { return (Shape3){ .kind = SHAPE_KIND_CONVEX_POLYGON, .as.convex = { vertices, vertex_count } }; }

static inline Shape3 shape3_from_aabb3(AABB3 a) { return (Shape3){ .kind = SHAPE_KIND_AABB3, .as.aabb3 = a }; }
static inline Shape3 shape3_from_convex3(ConvexPolygon3 c) { return (Shape3){ .kind = SHAPE_KIND_CONVEX_POLYGON, .as.convex = c }; }
static inline Shape3 shape3_from_sphere(Sphere s) { return (Shape3){ .kind = SHAPE_KIND_SPHERE, .as.sphere = s }; }
static inline Shape3 shape3_from_capsule3(Capsule3 s) { return (Shape3){ .kind = SHAPE_KIND_CAPSULE3, .as.capsule = s }; }
static inline Shape3 shape3_from_plane(Plane p) { return (Shape3){ .kind = SHAPE_KIND_PLANE, .as.plane = p }; }

Shape3 shape3_move(Shape3 s, float3 displacement);
float3 shape3_support(Shape3 s, float3 direction);

Raycast3Result shapecast(Shape3 a, Shape3 b, float3 direction, float max_distance);
Raycast3Result raycast_plane(float3 ro, float3 rd, float3 po, float3 pn);
Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 extent);

Raycast3Result sphere_sweep_triangle3(float3 origin, float radius, float3 direction, float max_distance, Triangle3 triangle);

float segment3_squared_distance(Segment3 segment, float3 point);

float3 plane_closest_point(Plane p, float3 to);
float3 segment3_closest_point(Segment3 segment, float3 to);
float3 triangle3_closest_point(Triangle3 t, float3 to);
float3 tetrahedron_closest_point(float3 a, float3 b, float3 c, float3 d, float3 to);
float3 aabb3_closest_point(AABB3 a, float3 to);

bool triangle3_contains_point(Triangle3 triangle, float3 point);

bool lowest_root(float a, float b, float c, float max_r, float *root);

typedef struct {
	float3 support_a[4], support_b[4], points[4];
	uint32_t point_count;
} Simplex3;

float3 simplex_closest_point_to_origin(Simplex3 simplex);

float3 simplex_line(Simplex3 *simplex, float3 *direction);
float3 simplex_triangle(Simplex3 *simplex, float3 *direction);
float3 simplex_tetrahedron(Simplex3 *simplex, float3 *direction);

extern float max_error;
float gjk_distance_squared(Shape3 a, Shape3 b, float reference_dist_sq);

#endif
