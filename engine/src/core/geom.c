#include "geom.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/geom_types.h"
#include <stdlib.h>
#include <strings.h>

// chapter 3, 5, 9, 11, 12

float3 barycentric(Triangle3 t, float3 p) {
	float3 result = { 0 };

	float3 ab = sub3(t.b, t.a);
	float3 ac = sub3(t.c, t.a);
	float3 ap = sub3(p, t.a);

	float ab_dot_ab = dot3(ab, ab);
	float ab_dot_ac = dot3(ab, ac);
	float ac_dot_ac = dot3(ac, ac);
	float ap_dot_ab = dot3(ap, ab);

	float ap_dot_ac = dot3(ap, ac);
	float denom = ab_dot_ab * ac_dot_ac - ab_dot_ac * ab_dot_ac;

	result.y = (ac_dot_ac * ap_dot_ab - ab_dot_ac * ap_dot_ac) / denom; // v
	result.z = (ab_dot_ab * ap_dot_ac - ab_dot_ac * ap_dot_ab) / denom; // w
	result.x = 1.0f - result.y - result.z; // u

	return result;
}

Shape3 shape3_displace(Shape3 s, float3 displacement) {
	Shape3 result = s;

	switch (s.kind) {
		case SHAPE_KIND_AABB3:
			result = shape3_from_aabb3(aabb3_move(s.as.aabb3, displacement));
			break;
		case SHAPE_KIND_SPHERE:
			result = shape3_sphere(add3(s.as.sphere.center, displacement), s.as.sphere.radius);
			break;
		case SHAPE_KIND_CAPSULE3:
			result = shape3_from_capsule(capsule3_move(s.as.capsule, displacement));
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

float3 shape3_furthest_point(const Shape3 *s, float3 direction) {
	float3 result = { 0 };

	direction = norm3(direction);
	ASSERT(lensq3(direction) > EPSILON * EPSILON);
	ASSERT(s != 0);

	switch (s->kind) {
		case SHAPE_KIND_AABB3: {
			const AABB3 *box = &s->as.aabb3;
			result = (float3){
				direction.x >= 0.0f ? box->max.x : box->min.x,
				direction.y >= 0.0f ? box->max.y : box->min.y,
				direction.z >= 0.0f ? box->max.z : box->min.z,
			};
		} break;

		case SHAPE_KIND_CAPSULE3: {
			const Capsule3 *capsule = &s->as.capsule;
			float adotd = dot3(capsule->a, direction);
			float bdotd = dot3(capsule->b, direction);
			float3 c = adotd > bdotd ? capsule->a : capsule->b;

			result = add3(c, scale3(direction, capsule->radius));
		} break;

		case SHAPE_KIND_SPHERE: {
			const Sphere *sphere = &s->as.sphere;
			result = add3(sphere->center, scale3(direction, sphere->radius));
		} break;

		case SHAPE_KIND_CONVEX_POLYGON: {
			const ConvexPolytope3 *polygon = &s->as.convex;
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

	return result;
}

typedef struct {
	float3 p, q, w;
} SimplexVertex;
typedef struct {
	SimplexVertex vertices[4];
	uint32_t count;
} Simplex;

INLINE void simplex_pushfront(Simplex *s, SimplexVertex point) {
	ASSERT(s && s->count < 4);

	for (uint32_t i = s->count; i > 0; --i)
		s->vertices[i] = s->vertices[i - 1];

	s->vertices[0] = point;
	++s->count;
}

SimplexVertex simplex_support(const Shape3 *a, const Shape3 *b, float3 d) {
	float3 p = shape3_furthest_point(a, d);
	float3 q = shape3_furthest_point(b, neg3(d));

	return (SimplexVertex){ p, q, sub3(p, q) };
}

#define simplex_make(...) simplex_make_impl(sizeof((SimplexVertex[]){ __VA_ARGS__ }) / sizeof(SimplexVertex), (SimplexVertex[]){ __VA_ARGS__ })
Simplex simplex_make_impl(uint64_t count, SimplexVertex *points) {
	ASSERT(count <= 4);

	Simplex result = { 0 };
	for (uint32_t index = 0; index < count; ++index)
		result.vertices[result.count++] = points[index];

	return result;
}

#define same_direction(a, b) dot3(a, b) > 0
bool simplex_line(Simplex *simplex, float3 *direction) {
	bool result = false;

	SimplexVertex a = simplex->vertices[0];
	SimplexVertex b = simplex->vertices[1];

	float3 ab = sub3(b.w, a.w);
	float3 ao = neg3(a.w);

	if (same_direction(ab, ao))
		*direction = cross3(cross3(ab, ao), ab);
	else {
		*simplex = simplex_make(a);
		*direction = ao;
	}

	return result;
}

bool simplex_triangle(Simplex *simplex, float3 *direction) {
	bool result = false;

	SimplexVertex a = simplex->vertices[0];
	SimplexVertex b = simplex->vertices[1];
	SimplexVertex c = simplex->vertices[2];

	float3 ab = sub3(b.w, a.w);
	float3 ac = sub3(c.w, a.w);
	float3 ao = neg3(a.w);

	float3 abc = cross3(ab, ac);

	if (same_direction(cross3(abc, ac), ao)) {
		if (same_direction(ac, ao)) {
			*simplex = simplex_make(a, c);
			*direction = cross3(cross3(ac, ao), ac);
		} else {
			*simplex = simplex_make(a, b);
			return simplex_line(simplex, direction);
		}
	} else {
		if (same_direction(cross3(ab, abc), ao)) {
			*simplex = simplex_make(a, b);
			return simplex_line(simplex, direction);
		} else {
			if (same_direction(abc, ao))
				*direction = abc;
			else {
				*simplex = simplex_make(a, c, b);
				*direction = neg3(abc);
			}
		}
	}

	return result;
}

bool simplex_tetrahedron(Simplex *simplex, float3 *direction) {
	bool result = true;

	SimplexVertex a = simplex->vertices[0];
	SimplexVertex b = simplex->vertices[1];
	SimplexVertex c = simplex->vertices[2];
	SimplexVertex d = simplex->vertices[3];

	float3 ab = sub3(b.w, a.w);
	float3 ac = sub3(c.w, a.w);
	float3 ad = sub3(d.w, a.w);
	float3 ao = neg3(a.w);

	float3 abc = cross3(ab, ac);
	float3 acd = cross3(ac, ad);
	float3 adb = cross3(ad, ab);

	if (same_direction(abc, ao)) {
		*simplex = simplex_make(a, b, c);
		return simplex_triangle(simplex, direction);
	}
	if (same_direction(acd, ao)) {
		*simplex = simplex_make(a, c, d);
		return simplex_triangle(simplex, direction);
	}
	if (same_direction(adb, ao)) {
		*simplex = simplex_make(a, d, b);
		return simplex_triangle(simplex, direction);
	}

	return result;
}

// src: Real-Time Collision Detection 5.4.2 Testing Point in Triangle
bool triangle3_contains_point(Triangle3 t, float3 p) {
	// Translate point and triangle so that point lies at origin
	float3 pa = sub3(t.a, p);
	float3 pb = sub3(t.b, p);
	float3 pc = sub3(t.c, p);

	float ab = dot3(pa, pb);
	float ac = dot3(pa, pc);
	float bc = dot3(pb, pc);
	float cc = dot3(pc, pc);

	// Make sure plane normals for pab and pbc point in the same direction
	if (bc * ac - cc * ab < 0.0f) return 0;
	// Make sure plane normals for pab and pca point in the same direction
	float bb = dot3(pb, pb);
	if (ab * bc - ac * bb < 0.0f) return 0;

	// Otherwise P must be in (or on) the triangle
	return 1;
}

bool gjk_overlap(const Shape3 *shape_a, const Shape3 *shape_b) {
	bool result = false;

	bool ok = shape_a && shape_b;
	if (ok) {
		float3 d = unit3(RIGHT);

		SimplexVertex a = simplex_support(shape_a, shape_b, d);
		Simplex s = simplex_make(a);
		d = neg3(a.w);

		while (true) {
			a = simplex_support(shape_a, shape_b, d);
			if (dot3(a.w, d) <= 0)
				break;
			simplex_pushfront(&s, a);

			bool collision;
			// clang-format off
			switch (s.count) {
				case 2: collision = simplex_line(&s, &d);        break;
				case 3: collision = simplex_triangle(&s, &d);    break;
				case 4: collision = simplex_tetrahedron(&s, &d); break;

				default:
					ASSERT(!"Invalid state");
			}
			if (collision) { result = true; break; }
			// clang-format on
		}

	} else
		LOG_WARN("%s - null parameter passed, skipping.", __func__);

	return result;
}
#undef same_direction

CastResult3 raycast_plane(Ray3 r, Plane p) {
	CastResult3 result = CAST3_NO_HIT;

	float denominator = dot3(r.direction, p.normal);
	float t = 0.0f;

	bool ok = fabsf(denominator) >= EPSILON;
	if (ok) {
		float3 po = scale3(p.normal, p.distance);
		t = (dot3(po, p.normal) - dot3(r.origin, p.normal)) / denominator;

		ok = t >= 0.0f;
	}

	if (ok) {
		float3 hit_point = add3(r.origin, scale3(r.direction, t));
		result = (CastResult3){ .hit = true, .t = t, .normal = p.normal, .point = hit_point };
	}

	return result;
}

CastResult3 raycast_aabb3(Ray3 r, AABB3 a) {
	float3 min = a.min;
	float3 max = a.max;

	CastResult3 result = CAST3_NO_HIT, temp = CAST3_NO_HIT;
	if (lensq3(r.direction)) {
		for (uint32_t side = 0; side < SIDE_MAX3; ++side) {
			float3 pn = side_to_float3[side];
			float3 po = add3(aabb3_center(a), mul3(pn, aabb3_half_extent(a)));

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

bool lowest_root(float a, float b, float c, float maxR, float *root) {
	float determinant = b * b - 4.0f * a * c;

	// If determinant is negative it means no solutions.
	if (determinant < 0.0f) return false;

	// calculate the two roots: (if determinant == 0 then
	// x1==x2 but let’s disregard that slight optimization)
	float sqrtD = sqrt(determinant);
	float r1 = (-b - sqrtD) / (2 * a);
	float r2 = (-b + sqrtD) / (2 * a);

	// Sort so x1 <= x2
	if (r1 > r2) swap(r1, r2, float);

	// Get lowest root:
	if (r1 > 0 && r1 < maxR) {
		*root = r1;
		return true;
	}

	// It is possible that we want x2 - this can happen
	// if x1 < 0
	if (r2 > 0 && r2 < maxR) {
		*root = r2;
		return true;
	}
	// No (valid) solutions
	return false;
}

CastResult3 spherecast_triangle(float3 center, float3 velocity, Triangle3 triangle) {
	CastResult3 result = CAST3_NO_HIT;

	bool embedded = false;
	Plane tp = plane_from_triangle(triangle);
	float t0, t1, signed_dist;

	float ndotv = dot3(tp.normal, velocity);
	float radius_sq = 1.0f;

	bool ok = dot3(velocity, velocity) > EPSILON && ndotv <= 0.0f;
	if (ok) {
		signed_dist = plane_signed_distance(tp, center);

		embedded = fabsf(signed_dist) < 1.0f;
		if (embedded) {
			t0 = 0.0f;
			t1 = 1.0f;
		}

		if (eqf(ndotv, 0.0f))
			ok = embedded;
		else {
			t0 = (-1.0f - signed_dist) / ndotv;
			t1 = (1.0f - signed_dist) / ndotv;
			if (t0 > t1)
				swap(t0, t1, float);

			ok = (t0 > 1.0f || t1 < 0.0f) == false;
		}
	}

	if (ok) { // collision between t0 - t1
		t0 = clampf(t0, 0.0f, 1.0f);
		t1 = clampf(t1, 0.0f, 1.0f);

		if (embedded == false) {
			float3 plane_intersect_point = add3(sub3(center, tp.normal), scale3(velocity, t0));
			if (triangle3_contains_point(triangle, plane_intersect_point)) {
				result.hit = true;
				result.t = t0;
				result.point = plane_intersect_point;
				result.normal = tp.normal;
			}
		}

		if (result.hit == false) {
			float velocity_length_sq = dot3(velocity, velocity);

			// a *t^2 + b*t + c = 0

			float3 vertices[] = { triangle.a, triangle.b, triangle.c };

			// vertices
			for (uint32_t vertex_index = 0; vertex_index < countof(vertices); ++vertex_index) {
				float3 vertex = vertices[vertex_index];
				float a = velocity_length_sq;

				float b = 2.0 * dot3(velocity, sub3(center, vertex));
				float c = lensq3(sub3(vertex, center)) - radius_sq;

				float temp_t;
				if (lowest_root(a, b, c, result.t, &temp_t)) {
					result.hit = true;
					result.t = temp_t;
					result.point = vertex;
					float3 sphere_center_at_t = add3(center, scale3(velocity, result.t));
					result.normal = norm3(sub3(sphere_center_at_t, result.point));
				}
			}

			// edges
			for (uint32_t edge_index = 0; edge_index < countof(vertices); ++edge_index) {
				float3 edge = sub3(vertices[(edge_index + 1) % 3], vertices[edge_index]);
				float3 vertex = vertices[edge_index];
				float3 origin_to_vertex = sub3(vertex, center);

				float edge_length_sq = lensq3(edge);
				float edge_dot_velocity = dot3(edge, velocity);
				float edge_dot_origin_to_vertex = dot3(edge, origin_to_vertex);

				float a = edge_length_sq * -velocity_length_sq + (edge_dot_velocity * edge_dot_velocity);
				float b = edge_length_sq * (2.0f * dot3(velocity, origin_to_vertex)) - 2.0 * edge_dot_velocity * edge_dot_origin_to_vertex;
				float c = edge_length_sq * (radius_sq - lensq3(origin_to_vertex)) + (edge_dot_origin_to_vertex * edge_dot_origin_to_vertex);

				float temp_t;
				if (lowest_root(a, b, c, result.t, &temp_t)) {
					float f = (edge_dot_velocity * temp_t - edge_dot_origin_to_vertex) / edge_length_sq;
					if (f >= 0.0f && f <= 1.0) {
						result.hit = true;
						result.t = temp_t;
						result.point = add3(vertex, scale3(edge, f));
						float3 sphere_center_at_t = add3(center, scale3(velocity, result.t));
						result.normal = norm3(sub3(sphere_center_at_t, result.point));
					}
				}
			}
		}

		if (result.t >= 1.0f) result = CAST3_NO_HIT;
	}

	return result;
}

bool project_to_viewport(float4x4 view_proj, Rectangle viewport, float3 point, float2 *screen) {
	float4 clip = mul4x4v(view_proj, make4_from3(point, 1.0f));

	bool ok = clip.w > EPSILON;
	if (ok) {
		float3 ndc = scale3(make3_from4(clip), 1.0f / clip.w);
		*screen = make2(
			viewport.x + ((ndc.x * 0.5f + 0.5f) * viewport.width),
			viewport.y + ((ndc.y * 0.5f + 0.5f) * viewport.height) //
		);
	}

	return ok;
}
