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
			result = shape3_from_plane(plane_from_point_normal(c, normalize3_safe(scale3(c, -1.0f), EPSILON)));
		} break;
		case SHAPE_KIND_CONVEX_POLYGON: {
			for (uint32_t index = 0; index < s.as.convex.vertex_count; ++index)
				s.as.convex.vertices[index] = add3(s.as.convex.vertices[index], displacement);
		} break;
	}

	return result;
}

float3 shape3_support(Shape3 s, float3 direction) {
	float3 result = { 0 };

	direction = normalize3_safe(direction, EPSILON);
	ASSERT(equalf(length3_sq(direction), 0.0f) == false);

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

Raycast3Result raycast_plane(float3 ro, float3 rd, float3 po, float3 pn) {
	float denominator = dot3(rd, pn);
	if (fabsf(denominator) < EPSILON)
		return RAY3_NO_HIT;

	float t = (dot3(po, pn) - dot3(ro, pn)) / denominator;

	if (t < 0.0f)
		return RAY3_NO_HIT;

	float3 point = add3(ro, scale3(rd, t));

	Raycast3Result result = {
		.hit = true,
		.t = t,
		.normal = pn,
		.point = point,
	};

	return result;
}

Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 half_extent) {
	float3 min = sub3(center, half_extent);
	float3 max = add3(center, half_extent);

	Raycast3Result result = RAY3_NO_HIT, temp = RAY3_NO_HIT;
	if (length3_sq(rd)) {
		// left
		float3 po = sub3(center, (float3){ half_extent.x, 0.0f, 0.0f });
		float3 pn = { -1.0f, 0.0f, 0.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;

		// right
		po = add3(center, (float3){ half_extent.x, 0.0f, 0.0f });
		pn = (float3){ 1.0f, 0.0f, 0.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;

		// front
		po = add3(center, (float3){ 0.0f, 0.0f, half_extent.z });
		pn = (float3){ 0.0f, 0.0f, 1.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;

		// back
		po = sub3(center, (float3){ 0.0f, 0.0f, half_extent.z });
		pn = (float3){ 0.0f, 0.0f, -1.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;

		// top
		po = add3(center, (float3){ 0.0f, half_extent.y, 0.0f });
		pn = (float3){ 0.0f, 1.0f, 0.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;

		// bottom
		po = sub3(center, (float3){ 0.0f, half_extent.y, 0.0f });
		pn = (float3){ 0.0f, -1.0f, 0.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;
	}

	return result;
}

Raycast3Result sphere_sweep_triangle3(float3 origin, float radius, float3 direction, float max_distance, Triangle3 triangle) {
	Raycast3Result result = RAY3_NO_HIT;
	result.t = 1.0f;

	bool embedded = false;
	float radius_sq = radius * radius;

	Plane triangle_plane = plane_from_triangle(triangle);
	float3 velocity = scale3(direction, max_distance);

	bool ok = dot3(triangle_plane.normal, direction) < 0.0f; // front facing
	if (ok) {
		float t0, t1;

		float signed_distance = plane_signed_distance(triangle_plane, origin);
		float ndotv = dot3(triangle_plane.normal, velocity);

		if (equalf(ndotv, 0.0f)) {
			if (fabsf(signed_distance) >= radius) {
				return result;
			} else {
				embedded = true;
			}
		} else {
			t0 = (-radius - signed_distance) / ndotv;
			t1 = (radius - signed_distance) / ndotv;

			if (t0 > t1) {
				float temp = t0;
				t0 = t1;
				t1 = temp;
			}

			if (t0 > 1.0f || t1 < 0.0f) {
				return result;
			} else {
				t0 = clampf(t0, 0.0f, 1.0f);
				t1 = clampf(t1, 0.0f, 1.0f);
			}
		}

		if (embedded == false) {
			float3 plane_contact_point =
				add3(sub3(origin, scale3(triangle_plane.normal, radius)), scale3(velocity, t0));

			if (triangle3_contains_point(triangle, plane_contact_point)) {
				result.hit = true;
				result.t = t0;
				result.point = plane_contact_point;
				result.normal = normalize3_safe(triangle_plane.normal, EPSILON);
				return result;
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

				float b = 2.0 * dot3(velocity, sub3(origin, vertex));
				float c = length3_sq(sub3(vertex, origin)) - radius_sq;

				float temp_t;
				if (lowest_root(a, b, c, result.t, &temp_t)) {
					result.hit = true;
					result.t = temp_t;
					result.point = vertex;
					float3 sphere_center_at_t = add3(origin, scale3(velocity, result.t));
					result.normal = normalize3(sub3(sphere_center_at_t, result.point));
				}
			}

			// edges
			for (uint32_t edge_index = 0; edge_index < countof(vertices); ++edge_index) {
				float3 edge = sub3(vertices[(edge_index + 1) % 3], vertices[edge_index]);
				float3 vertex = vertices[edge_index];
				float3 origin_to_vertex = sub3(vertex, origin);

				float edge_length_sq = length3_sq(edge);
				float edge_dot_velocity = dot3(edge, velocity);
				float edge_dot_origin_to_vertex = dot3(edge, origin_to_vertex);

				float a = edge_length_sq * -velocity_length_sq + (edge_dot_velocity * edge_dot_velocity);
				float b = edge_length_sq * (2.0f * dot3(velocity, origin_to_vertex)) - 2.0 * edge_dot_velocity * edge_dot_origin_to_vertex;
				float c = edge_length_sq * (radius_sq - length3_sq(origin_to_vertex)) + (edge_dot_origin_to_vertex * edge_dot_origin_to_vertex);

				float temp_t;
				if (lowest_root(a, b, c, result.t, &temp_t)) {
					float f = (edge_dot_velocity * temp_t - edge_dot_origin_to_vertex) / edge_length_sq;
					if (f >= 0.0f && f <= 1.0) {
						result.hit = true;
						result.t = temp_t;
						result.point = add3(vertex, scale3(edge, f));
						float3 sphere_center_at_t = add3(origin, scale3(velocity, result.t));
						result.normal = normalize3(sub3(sphere_center_at_t, result.point));
					}
				}
			}
		}
	}

	return result;
}

float segment3_squared_distance(Segment3 segment, float3 point) {
	float3 ab = sub3(segment.b, segment.a);
	float3 ap = sub3(point, segment.a);

	float t = dot3(ap, ab);
	if (t <= 0.0f)
		return dot3(ap, ap);

	float length_squared = dot3(ab, ab);
	if (t >= length_squared) {
		float3 bp = sub3(point, segment.b);
		return dot3(bp, bp);
	}

	return dot3(ap, ap) - (t * t) / length_squared;
}

// R = R - (n dot (R - P))n = R = R - tn
float3 plane_closest_point(Plane p, float3 point) {
	float t = (dot3(point, p.normal) - p.distance) / length3_sq(p.normal);
	return sub3(point, scale3(p.normal, t));
}

float3 segment3_closest_point(Segment3 segment, float3 point) {
	float3 result = { 0 };
	float3 ab = sub3(segment.b, segment.a);

	float t = dot3(sub3(point, segment.a), ab);
	if (t <= 0.0f) {
		result = segment.a;
	} else {
		float denominator = length3_sq(ab);
		if (t >= denominator) {
			result = segment.b;
		} else {
			t = t / denominator;
			result = add3(segment.a, scale3(ab, t));
		}
	}

	return result;
}

float3 triangle3_closest_point(Triangle3 t, float3 p) {
	float3 result = { 0 };

	float3 proj = plane_project_point(plane_from_triangle(t), p);
	if (triangle3_contains_point(t, proj)) {
		return proj;
	}

	float3 ab_point = segment3_closest_point((Segment3){ t.a, t.b }, p);
	float3 bc_point = segment3_closest_point((Segment3){ t.b, t.c }, p);
	float3 ca_point = segment3_closest_point((Segment3){ t.c, t.a }, p);

	float ab_len_sq = length3_sq(sub3(ab_point, p));
	float bc_len_sq = length3_sq(sub3(bc_point, p));
	float ca_len_sq = length3_sq(sub3(ca_point, p));

	if (ab_len_sq < bc_len_sq && ab_len_sq < ca_len_sq)
		result = ab_point;
	else if (bc_len_sq < ca_len_sq)
		result = bc_point;
	else
		result = ca_point;

	return result;
}

static bool tetrahedron_face_is_candidate(float3 a, float3 b, float3 c, float3 opposite, float3 p) {
	float3 n = plane_from_triangle((Triangle3){ a, b, c }).normal;
	float side_opposite = dot3(n, sub3(opposite, a));
	float side_p = dot3(n, sub3(p, a));
	return side_opposite * side_p <= 0.0f;
}

float3 tetrahedron_closest_point(float3 a, float3 b, float3 c, float3 d, float3 p) {
	float closest_square_distance = FLOAT_MAX;
	float3 result = p;

	struct {
		float3 a, b, c, opposite;
	} faces[4] = {
		{ a, b, c, d },
		{ a, c, d, b },
		{ a, d, b, c },
		{ b, d, c, a },
	};

	for (int face_indx = 0; face_indx < 4; face_indx++) {
		if (tetrahedron_face_is_candidate(faces[face_indx].a, faces[face_indx].b, faces[face_indx].c, faces[face_indx].opposite, p)) {
			Triangle3 tri = { faces[face_indx].a, faces[face_indx].b, faces[face_indx].c };
			float3 q = triangle3_closest_point(tri, p);
			float dist = length3_sq(sub3(q, p));
			if (dist < closest_square_distance) {
				result = q;
				closest_square_distance = dist;
			}
		}
	}

	return result;
}

float3 aabb3_closest_point(AABB3 a, float3 to) {
	float3 result = { 0 };

	result.x = clampf(to.x, a.min.x, a.max.x);
	result.y = clampf(to.y, a.min.y, a.max.y);
	result.z = clampf(to.z, a.min.z, a.max.z);

	return result;
}

bool triangle3_contains_point(Triangle3 t, float3 p) {
	Plane plane = plane_from_triangle(t);

	// 1. Ensure the point is on the triangle's infinite plane
	if (!equalf(dot3(plane.normal, p), plane.distance)) {
		return false;
	}

	float3 a = sub3(t.a, p);
	float3 b = sub3(t.b, p);
	float3 c = sub3(t.c, p);

	float3 norm_pab = cross3(a, b);
	float3 norm_pbc = cross3(b, c);
	float3 norm_pca = cross3(c, a);

	// 2. Test each edge cross-product against the actual face normal
	bool sign_ab = dot3(norm_pab, plane.normal) >= 0.0f;
	bool sign_bc = dot3(norm_pbc, plane.normal) >= 0.0f;
	bool sign_ca = dot3(norm_pca, plane.normal) >= 0.0f;

	// All three must match direction (handles both CW and CCW winding safety)
	return (sign_ab == sign_bc) && (sign_bc == sign_ca);
}

bool lowest_root(float a, float b, float c, float max_r, float *root) {
	float determinant = b * b - 4.0f * a * c;

	if (determinant < 0.0f)
		return false;

	float square_root = sqrtf(determinant);
	float r0 = (-b - square_root) / (2 * a);
	float r1 = (-b + square_root) / (2 * a);

	if (r0 > r1) {
		float temp = r0;
		r0 = r1;
		r1 = temp;
	}

	if (r0 > 0 && r0 < max_r) {
		*root = r0;
		return true;
	}

	if (r1 > 0 && r1 < max_r) {
		*root = r1;
		return true;
	}

	return false;
}

float3 simplex_closest_point_to_origin(Simplex3 simplex) {
	float3 result = { 0 };

	float3 origin = { 0 };
	switch (simplex.point_count) {
		case 1: {
			float3 a = simplex.points[0];
			result = a;
		} break;
		case 2: {
			float3 a = simplex.points[1];
			float3 b = simplex.points[0];
			result = segment3_closest_point((Segment3){ a, b }, origin);
		} break;
		case 3: {
			float3 a = simplex.points[2];
			float3 b = simplex.points[1];
			float3 c = simplex.points[0];
			result = triangle3_closest_point((Triangle3){ a, b, c }, origin);
		} break;
		case 4: {
			float3 a = simplex.points[3];
			float3 b = simplex.points[2];
			float3 c = simplex.points[1];
			float3 d = simplex.points[0];
			result = tetrahedron_closest_point(a, b, c, d, origin);
		} break;
		default:
			ASSERT(!"Invalid simplex point count");
	}

	return result;
}

void simplex_update(Simplex3 *simplex, uint32_t *new, uint32_t count) {
	Simplex3 result = { .point_count = count };

	bool ok = simplex && new && count;
	ASSERT(ok);

	for (uint32_t index = 0; index < count; ++index) {
		uint32_t override_index = new[index];
		ASSERT(override_index < simplex->point_count);

		result.points[index] = simplex->points[override_index];
		result.support_a[index] = simplex->support_a[override_index];
		result.support_b[index] = simplex->support_b[override_index];
	}

	*simplex = result;
}

float3 simplex_line(Simplex3 *simplex, float3 *direction) {
	float3 origin = { 0 };

	float3 a = simplex->points[simplex->point_count - 1];
	float3 b = simplex->points[simplex->point_count - 2];

	float3 ab = sub3(b, a);
	float3 ao = sub3(origin, a);

	float3 result = { 0 };
	float ab_len_sq = length3_sq(ab);
	if (ab_len_sq > EPSILON && dot3(ab, ao) > 0.0f) {
		*direction = cross3(cross3(ab, ao), ab);
		float t = dot3(ao, ab) / ab_len_sq;
		result = add3(a, scale3(ab, t));
	} else {
		simplex_update(simplex, array_arg(uint32_t, 1));
		*direction = ao;
		result = a;
	}

	return result;
}

float3 simplex_triangle(Simplex3 *simplex, float3 *direction) {
	float3 origin = { 0 };

	float3 a = simplex->points[simplex->point_count - 1];
	float3 b = simplex->points[simplex->point_count - 2];
	float3 c = simplex->points[simplex->point_count - 3];

	float3 ab = sub3(b, a);
	float3 ac = sub3(c, a);
	float3 ao = sub3(origin, a);

	float3 abc = cross3(ab, ac);
	float abc_len_sq = length3_sq(abc);

	if (abc_len_sq < EPSILON) { // triangle is colinear
		if (length3_sq(ab) > length3_sq(ac)) {
			simplex_update(simplex, array_arg(uint32_t, 1, 2));
			return simplex_line(simplex, direction);
		} else {
			simplex_update(simplex, array_arg(uint32_t, 0, 2));
			return simplex_line(simplex, direction);
		}
	}

	float3 result = { 0 };
	if (dot3(cross3(abc, ac), ao) > 0.0f) {
		if (dot3(ac, ao) > 0.0f) {
			simplex_update(simplex, array_arg(uint32_t, 0, 2));
			*direction = cross3(cross3(ac, ao), ac);

			float t = dot3(ao, ac) / dot3(ac, ac);
			result = add3(a, scale3(ac, t));
		} else {
			simplex_update(simplex, array_arg(uint32_t, 1, 2));
			result = simplex_line(simplex, direction);
		}
	} else {
		if (dot3(cross3(ab, abc), ao) > 0.0f) {
			simplex_update(simplex, array_arg(uint32_t, 1, 2));
			result = simplex_line(simplex, direction);
		} else {
			if (dot3(abc, ao) > 0.0f) {
				*direction = abc;
				result = plane_project_point(plane_from_triangle((Triangle3){ a, b, c }), origin);
			} else {
				simplex_update(simplex, array_arg(uint32_t, 1, 0, 2));
				*direction = scale3(abc, -1.0f);
				result = plane_project_point(plane_from_triangle((Triangle3){ b, c, a }), origin);
			}
			/* result = float3_subtract(origin, float3_scale(abc, float3_dot(ao, abc) / float3_dot(abc, abc))); */
		}
	}

	return result;
}

float3 simplex_tetrahedron(Simplex3 *simplex, float3 *direction) {
	float3 origin = { 0 };

	float3 a = simplex->points[simplex->point_count - 1];
	float3 b = simplex->points[simplex->point_count - 2];
	float3 c = simplex->points[simplex->point_count - 3];
	float3 d = simplex->points[simplex->point_count - 4];

	float3 ab = sub3(b, a);
	float3 ac = sub3(c, a);
	float3 ad = sub3(d, a);
	float3 ao = sub3(origin, a);

	float3 abc = cross3(ab, ac);
	float3 acd = cross3(ac, ad);
	float3 adb = cross3(ad, ab);

	bool abc_direction = dot3(abc, ao) > 0.0f;
	bool acd_direction = dot3(acd, ao) > 0.0f;
	bool adb_direction = dot3(adb, ao) > 0.0f;

	if (abc_direction == false && acd_direction == false && adb_direction == false) {
		*direction = (float3){ 0 };
		return origin;
	}

	if (abc_direction && acd_direction == false && adb_direction == false) {
		simplex_update(simplex, array_arg(uint32_t, 1, 2, 3));
		return simplex_triangle(simplex, direction);
	}

	if (abc_direction == false && acd_direction && adb_direction == false) {
		simplex_update(simplex, array_arg(uint32_t, 0, 1, 3));
		return simplex_triangle(simplex, direction);
	}

	if (abc_direction == false && acd_direction == false && adb_direction) {
		simplex_update(simplex, array_arg(uint32_t, 2, 0, 3));
		return simplex_triangle(simplex, direction);
	}

	Simplex3 abc_simplex = *simplex;
	simplex_update(&abc_simplex, array_arg(uint32_t, 1, 2, 3));

	Simplex3 acd_simplex = *simplex;
	simplex_update(&acd_simplex, array_arg(uint32_t, 0, 1, 3));

	Simplex3 adb_simplex = *simplex;
	simplex_update(&adb_simplex, array_arg(uint32_t, 2, 0, 3));

	float3 d_abc = *direction;
	float3 p_abc = simplex_triangle(&abc_simplex, &d_abc);

	float3 d_acd = *direction;
	float3 p_acd = simplex_triangle(&acd_simplex, &d_acd);

	float3 d_adb = *direction;
	float3 p_adb = simplex_triangle(&adb_simplex, &d_adb);

	float abc_d2 = dot3(p_abc, p_abc);
	float acd_d2 = dot3(p_acd, p_acd);
	float adb_d2 = dot3(p_adb, p_adb);

	if (abc_d2 <= acd_d2 && abc_d2 <= adb_d2) {
		*direction = d_abc;
		*simplex = abc_simplex;
		return p_abc;
	} else if (acd_d2 <= abc_d2 && acd_d2 <= adb_d2) {
		*direction = d_acd;
		*simplex = acd_simplex;
		return p_acd;
	} else if (adb_d2 <= abc_d2 && adb_d2 <= acd_d2) {
		*direction = d_adb;
		*simplex = adb_simplex;
		return p_adb;
	}

	ASSERT(false);

	return origin;
}

float max_error = 0.0f;
float gjk_distance_squared(Shape3 a, Shape3 b, float reference_dist_sq) {
	float tolerance = 0.001;
	float max_lower_bound = 0.0f;

	float3 sa = shape3_support(a, (float3){ 1.0f, 0.0f, 0.0f });
	float3 sb = shape3_support(b, (float3){ -1.0f, 0.0f, 0.0f });
	float3 support = sub3(sa, sb);
	float3 direction = scale3(support, -1.0f);

	float3 v = support;
	float closest_distance = length3_sq(v);

	Simplex3 simplex = { 0 };
	simplex.support_a[simplex.point_count] = sa;
	simplex.support_b[simplex.point_count] = sb;
	simplex.points[simplex.point_count++] = support;

	while (true) {
		if (closest_distance <= tolerance * tolerance)
			return 0.0f;

		float direction_length_sq = length3_sq(direction);
		if (direction_length_sq <= tolerance * tolerance)
			return max_lower_bound * max_lower_bound;

		float3 sa = shape3_support(a, direction);
		float3 sb = shape3_support(b, scale3(direction, -1.0f));
		float3 w = sub3(sa, sb);

		float upper_bound = sqrtf(closest_distance);

		// Project the new support point (w) onto the normalized closest point vector
		float lower_bound = dot3(v, w) / length3(v);
		if (lower_bound > max_lower_bound)
			max_lower_bound = lower_bound;

		// the gap between the unsampled space is smaller than our tolerance
		if (upper_bound - max_lower_bound <= tolerance)
			return max_lower_bound * max_lower_bound;

		simplex.support_a[simplex.point_count] = sa;
		simplex.support_b[simplex.point_count] = sb;
		simplex.points[simplex.point_count++] = w;

		if (simplex.point_count == 2) // line
			v = simplex_line(&simplex, &direction);
		else if (simplex.point_count == 3) // triangle
			v = simplex_triangle(&simplex, &direction);
		else if (simplex.point_count == 4) // tetrahedron
			v = simplex_tetrahedron(&simplex, &direction);
		else {
			ASSERT(!"Invalid state for 3D GJK");
		}

		float len_sq = length3_sq(v);
		if (len_sq < closest_distance)
			closest_distance = len_sq;
	}

	return max_lower_bound * max_lower_bound;
}
