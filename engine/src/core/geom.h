#ifndef GEOM_H_
#define GEOM_H_

#include "common.h"
#include "core/strings.h"
#include "core/geom_types.h"

static inline bool rect_contains(Rectangle rect, float x, float y) { return x > rect.x && x < rect.x + rect.width && y > rect.y && y < rect.y + rect.height; }
static inline bool rect_contains_point(Rectangle rect, float2 position) { return rect_contains(rect, position.x, position.y); }
static inline Rectangle rect_from_center(float2 center, float2 half_extent) { return (Rectangle){ center.x - half_extent.x, center.y - half_extent.y, half_extent.x * 2.0f, half_extent.y * 2.0f }; }
// :2d

static inline float3 ray_at(Ray3 r, float t) { return add3(r.origin, scale3(r.direction, t)); }

static inline Triangle3 triangle3_move(Triangle3 t, float3 offset) { return (Triangle3){ add3(offset, t.a), add3(offset, t.b), add3(offset, t.c) }; }

static inline AABB3 aabb3_from_center(float3 center, float3 half_extent) { return (AABB3){ .min = sub3(center, half_extent), .max = add3(center, half_extent) }; }
static inline float3 aabb3_center(AABB3 a) { return scale3(add3(a.min, a.max), 0.5f); }
static inline float3 aabb3_extent(AABB3 a) { return sub3(a.max, a.min); }
static inline float3 aabb3_half_extent(AABB3 a) { return scale3(aabb3_extent(a), 0.5f); }
static inline AABB3 aabb3_empty(void) { return (AABB3){ .min = splat3(FLOAT_MAX), .max = splat3(-FLOAT_MAX) }; }
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
static inline Sphere sphere_from_aabb3(AABB3 a) { return (Sphere){ aabb3_center(a), len3(aabb3_half_extent(a)) }; }
static inline bool sphere_contains_point(Sphere s, float3 p) { return len3_sq(sub3(p, s.center)) <= s.radius * s.radius; }

static inline Plane plane_from_side(Side s) { return (Plane){ side_to_float3[s], 0.0f }; }
static inline Plane plane_from_point_normal(float3 point, float3 normal) { return (Plane){ normal, dot3(point, normal) }; }
static inline float3 plane_center(Plane p) { return scale3(p.normal, p.distance); }
static inline float plane_signed_distance(Plane p, float3 point) { return dot3(p.normal, point) - p.distance; }
static inline float3 plane_project_point(Plane p, float3 point) { return sub3(point, scale3(p.normal, plane_signed_distance(p, point))); }
static inline Plane plane_from_triangle(Triangle3 t) {
	Plane result = { 0 };

	result.normal = norm3(cross3(sub3(t.b, t.a), sub3(t.c, t.a)));
	result.distance = dot3(result.normal, t.a);

	return result;
}

static inline Capsule3 capsule3_move(Capsule3 c, float3 displacement) { return (Capsule3){ add3(c.a, displacement), add3(c.b, displacement), c.radius }; }

static inline Capsule3 capsule_from_center(float3 center, float3 up, float height, float radius) {
	float actual_height = fmaxf(height, radius * 2.0f);

	float3 offset = scale3(up, (actual_height * 0.5f) - radius);
	return (Capsule3){ sub3(center, offset), add3(center, offset), radius };
}

static inline ConvexPolygon3 convex3_from_triangle3(Triangle3 *triangle) {
	ConvexPolygon3 result = {
		.vertices = &triangle->a,
		.vertex_count = 3,
	};

	return result;
}

static inline Shape3 shape3_sphere(float3 center, float radius) { return (Shape3){ .kind = SHAPE_KIND_SPHERE, .as.sphere = { .center = center, .radius = radius } }; }
static inline Shape3 shape3_capsule(float3 center, float height, float radius) { return (Shape3){ .kind = SHAPE_KIND_CAPSULE3, .as.capsule = capsule_from_center(center, unit3(UP), height, radius) }; }
static inline Shape3 shape3_convex_polygon(float3 *vertices, uint32_t vertex_count) { return (Shape3){ .kind = SHAPE_KIND_CONVEX_POLYGON, .as.convex = { vertices, vertex_count } }; }

static inline Shape3 shape3_from_aabb3(AABB3 a) { return (Shape3){ .kind = SHAPE_KIND_AABB3, .as.aabb3 = a }; }
static inline Shape3 shape3_from_convex3(ConvexPolygon3 c) { return (Shape3){ .kind = SHAPE_KIND_CONVEX_POLYGON, .as.convex = c }; }
static inline Shape3 shape3_from_sphere(Sphere s) { return (Shape3){ .kind = SHAPE_KIND_SPHERE, .as.sphere = s }; }
static inline Shape3 shape3_from_capsule3(Capsule3 s) { return (Shape3){ .kind = SHAPE_KIND_CAPSULE3, .as.capsule = s }; }
static inline Shape3 shape3_from_plane(Plane p) { return (Shape3){ .kind = SHAPE_KIND_PLANE, .as.plane = p }; }

Shape3 shape3_move(Shape3 s, float3 displacement);
float3 shape3_support(Shape3 s, float3 direction);

static inline bool aabb3_contains_point(AABB3 a, float3 p) {
	return (p.x > a.min.x && p.x < a.max.x) &&
		(p.y > a.min.y && p.y < a.max.x) &&
		(p.z > a.min.z && p.z < a.max.z);
}

CastResult3 raycast_plane(Ray3 r, Plane p);
CastResult3 raycast_aabb3(Ray3 r, AABB3 a);

bool project_to_viewport(float4x4 view_proj, Rectangle viewport, float3 point, float2 *screen); 
// :3d

#endif
