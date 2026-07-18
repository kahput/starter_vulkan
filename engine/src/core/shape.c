#include "shape3.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"

Shape3 shape3_move(Shape3 s, float3 displacement) {
	Shape3 result = s;

	switch (s.kind) {
		case SHAPE_KIND_AABB3:
			result = shape3_from_aabb3(aabb3_move(s.as.aabb3, displacement));
			break;
		case SHAPE_KIND_SPHERE:
			result = shape3_sphere(add3(s.as.sphere.center, displacement), s.as.sphere.radius);
			break;
		case SHAPE_KIND_CAPSULE3:
			result = shape3_from_capsule3(capsule3_move(s.as.capsule, displacement));
			break;
		case SHAPE_KIND_PLANE: {
			float3 c = add3(plane_center(s.as.plane), displacement);
			result = shape3_from_plane(plane_from_point_normal(c, norm3(scale3(c, -1.0f))));
		} break;
		case SHAPE_KIND_CONVEX_POLYGON: {
			for (uint32_t index = 0; index < s.as.convex.vertex_count; ++index)
				s.as.convex.vertices[index] = add3(s.as.convex.vertices[index], displacement);
		} break;
		case SHAPE_KIND_MAX:
			break;
	}

	return result;
}

float3 shape3_support(Shape3 s, float3 direction) {
	float3 result = { 0 };

	direction = norm3(direction);
	ASSERT(equalf(lensq3(direction), 0.0f) == false);

	ArenaTemp scratch = arena_scratch_begin(0);

	if (s.kind == SHAPE_KIND_AABB3)
		s = shape3_from_convex3(convex3_from_aabb3(scratch.arena, s.as.aabb3));

	switch (s.kind) {
		case SHAPE_KIND_CAPSULE3: {
			Capsule3 *capsule = &s.as.capsule;
			float adotd = dot3(capsule->a, direction);
			float bdotd = dot3(capsule->b, direction);
			result = adotd > bdotd
				? add3(capsule->a, scale3(direction, capsule->radius))
				: add3(capsule->b, scale3(direction, capsule->radius));
		} break;
		case SHAPE_KIND_SPHERE: {
			Sphere *sphere = &s.as.sphere;
			result = add3(sphere->center, scale3(direction, sphere->radius));
		} break;
		case SHAPE_KIND_CONVEX_POLYGON: {
			ConvexPolygon3 *polygon = &s.as.convex;
			ASSERT(polygon->vertices && polygon->vertex_count);

			result = polygon->vertices[0];
			float best_distance = dot3(result, direction);
			for (uint32_t index = 1; index < polygon->vertex_count; ++index) {
				float distance = dot3(polygon->vertices[index], direction);
				if (distance > best_distance) {
					best_distance = distance;
					result = polygon->vertices[index];
				}
			}
		} break;
		default:
			ASSERT(!"Unsupported shape support function");
			break;
	}

	arena_scratch_end(scratch);
	return result;
}

CastResult3 raycast_plane(Ray3 r, Plane p) {
	float denominator = dot3(r.direction, p.normal);
	if (fabsf(denominator) < EPSILON)
		return CAST3_NO_HIT;

	float3 po = scale3(p.normal, p.distance);
	float t = (dot3(po, p.normal) - dot3(r.origin, p.normal)) / denominator;

	if (t < 0.0f)
		return CAST3_NO_HIT;

	float3 point = add3(r.origin, scale3(r.direction, t));

	CastResult3 result = {
		.hit = true,
		.t = t,
		.normal = p.normal,
		.point = point,
	};

	return result;
}

CastResult3 raycast_aabb3(Ray3 r, AABB3 a) {
	float3 c = aabb3_center(a);
	float3 he = aabb3_half_extent(a);
	float3 min = a.min;
	float3 max = a.max;

	CastResult3 result = CAST3_NO_HIT, temp = CAST3_NO_HIT;
	if (lensq3(r.direction)) {
		for (uint32_t side = 0; side < SIDE_COUNT3; ++side) {
			float3 pn = side_to_float3[side];
			float3 po = add3(c, mul3(pn, he));

			temp = raycast_plane(r, plane_from_point_normal(po, pn));
			if ((temp.point.x < min.x || temp.point.x > max.x) ||
				(temp.point.y < min.y || temp.point.y > max.y) ||
				(temp.point.z < min.z || temp.point.z > max.z)) {
				temp = CAST3_NO_HIT;
			}
			if (temp.t < result.t)
				result = temp;
		}
	}

	return result;
}
