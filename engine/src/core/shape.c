#include "shape3.h"
#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"

float3 shape3_support(Shape3 s, float3 direction) {
	float3 result = { 0 };

	direction = float3_normalize_safe(direction, EPSILON);
	ASSERT(equalf(float3_length_sq(direction), 0.0f) == false);

	ArenaTemp scratch = arena_scratch_begin(0);

	if (s.kind == SHAPE_KIND_AABB3)
		s = shape3_from_convex3(convex3_from_aabb3(scratch.arena, s.as.aabb3));

	switch (s.kind) {
		case SHAPE_KIND_CAPSULE3: {
			Capsule3 *capsule = &s.as.capsule;
			float adotd = float3_dot(capsule->a, direction);
			float bdotd = float3_dot(capsule->b, direction);
			result = adotd > bdotd
				? float3_add(capsule->a, float3_scale(direction, capsule->radius))
				: float3_add(capsule->b, float3_scale(direction, capsule->radius));
		} break;
		case SHAPE_KIND_SPHERE: {
			Sphere *sphere = &s.as.sphere;
			result = float3_add(sphere->center, float3_scale(direction, sphere->radius));
		} break;
		case SHAPE_KIND_CONVEX_POLYGON: {
			ConvexPolygon3 *polygon = &s.as.convex;
			ASSERT(polygon->vertices && polygon->vertex_count);

			result = polygon->vertices[0];
			float best_distance = float3_dot(result, direction);
			for (uint32_t index = 1; index < polygon->vertex_count; ++index) {
				float distance = float3_dot(polygon->vertices[index], direction);
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
	float denominator = float3_dot(rd, pn);
	if (fabsf(denominator) < EPSILON)
		return RAY3_NO_HIT;

	float t = (float3_dot(po, pn) - float3_dot(ro, pn)) / denominator;

	if (t < 0.0f)
		return RAY3_NO_HIT;

	float3 point = float3_add(ro, float3_scale(rd, t));

	Raycast3Result result = {
		.hit = true,
		.t = t,
		.normal = pn,
		.point = point,
	};

	return result;
}

Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 half_extent) {
	float3 min = float3_subtract(center, half_extent);
	float3 max = float3_add(center, half_extent);

	Raycast3Result result = RAY3_NO_HIT, temp = RAY3_NO_HIT;
	if (float3_length_sq(rd)) {
		// left
		float3 po = float3_subtract(center, (float3){ half_extent.x, 0.0f, 0.0f });
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
		po = float3_add(center, (float3){ half_extent.x, 0.0f, 0.0f });
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
		po = float3_add(center, (float3){ 0.0f, 0.0f, half_extent.z });
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
		po = float3_subtract(center, (float3){ 0.0f, 0.0f, half_extent.z });
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
		po = float3_add(center, (float3){ 0.0f, half_extent.y, 0.0f });
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
		po = float3_subtract(center, (float3){ 0.0f, half_extent.y, 0.0f });
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
	float3 velocity = float3_scale(direction, max_distance);

	bool ok = float3_dot(triangle_plane.normal, direction) < 0.0f; // front facing
	if (ok) {
		float t0, t1;

		float signed_distance = plane_signed_distance(triangle_plane, origin);
		float ndotv = float3_dot(triangle_plane.normal, velocity);

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
				float3_add(float3_subtract(origin, float3_scale(triangle_plane.normal, radius)), float3_scale(velocity, t0));

			if (triangle3_contains_point(triangle, plane_contact_point)) {
				result.hit = true;
				result.t = t0;
				result.point = plane_contact_point;
				result.normal = float3_normalize_safe(triangle_plane.normal, EPSILON);
				return result;
			}
		}

		if (result.hit == false) {
			float velocity_length_sq = float3_dot(velocity, velocity);

			// a *t^2 + b*t + c = 0

			float3 vertices[] = { triangle.a, triangle.b, triangle.c };

			// vertices
			for (uint32_t vertex_index = 0; vertex_index < countof(vertices); ++vertex_index) {
				float3 vertex = vertices[vertex_index];
				float a = velocity_length_sq;

				float b = 2.0 * float3_dot(velocity, float3_subtract(origin, vertex));
				float c = float3_length_sq(float3_subtract(vertex, origin)) - radius_sq;

				float temp_t;
				if (lowest_root(a, b, c, result.t, &temp_t)) {
					result.hit = true;
					result.t = temp_t;
					result.point = vertex;
					float3 sphere_center_at_t = float3_add(origin, float3_scale(velocity, result.t));
					result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
				}
			}

			// edges
			for (uint32_t edge_index = 0; edge_index < countof(vertices); ++edge_index) {
				float3 edge = float3_subtract(vertices[(edge_index + 1) % 3], vertices[edge_index]);
				float3 vertex = vertices[edge_index];
				float3 origin_to_vertex = float3_subtract(vertex, origin);

				float edge_length_sq = float3_length_sq(edge);
				float edge_dot_velocity = float3_dot(edge, velocity);
				float edge_dot_origin_to_vertex = float3_dot(edge, origin_to_vertex);

				float a = edge_length_sq * -velocity_length_sq + (edge_dot_velocity * edge_dot_velocity);
				float b = edge_length_sq * (2.0f * float3_dot(velocity, origin_to_vertex)) - 2.0 * edge_dot_velocity * edge_dot_origin_to_vertex;
				float c = edge_length_sq * (radius_sq - float3_length_sq(origin_to_vertex)) + (edge_dot_origin_to_vertex * edge_dot_origin_to_vertex);

				float temp_t;
				if (lowest_root(a, b, c, result.t, &temp_t)) {
					float f = (edge_dot_velocity * temp_t - edge_dot_origin_to_vertex) / edge_length_sq;
					if (f >= 0.0f && f <= 1.0) {
						result.hit = true;
						result.t = temp_t;
						result.point = float3_add(vertex, float3_scale(edge, f));
						float3 sphere_center_at_t = float3_add(origin, float3_scale(velocity, result.t));
						result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
					}
				}
			}
		}
	}

	return result;
}

float segment3_squared_distance(Segment3 segment, float3 point) {
	float3 ab = float3_subtract(segment.b, segment.a);
	float3 ap = float3_subtract(point, segment.a);

	float t = float3_dot(ap, ab);
	if (t <= 0.0f)
		return float3_dot(ap, ap);

	float length_squared = float3_dot(ab, ab);
	if (t >= length_squared) {
		float3 bp = float3_subtract(point, segment.b);
		return float3_dot(bp, bp);
	}

	return float3_dot(ap, ap) - (t * t) / length_squared;
}

// R = R - (n dot (R - P))n = R = R - tn
float3 plane_closest_point(Plane p, float3 point) {
	float t = (float3_dot(point, p.normal) - p.distance) / float3_length_sq(p.normal);
	return float3_subtract(point, float3_scale(p.normal, t));
}

float3 segment3_closest_point(Segment3 segment, float3 point) {
	float3 result = { 0 };
	float3 ab = float3_subtract(segment.b, segment.a);

	float t = float3_dot(float3_subtract(point, segment.a), ab);
	if (t <= 0.0f) {
		result = segment.a;
	} else {
		float denominator = float3_length_sq(ab);
		if (t >= denominator) {
			result = segment.b;
		} else {
			t = t / denominator;
			result = float3_add(segment.a, float3_scale(ab, t));
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

	float ab_len_sq = float3_length_sq(float3_subtract(ab_point, p));
	float bc_len_sq = float3_length_sq(float3_subtract(bc_point, p));
	float ca_len_sq = float3_length_sq(float3_subtract(ca_point, p));

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
	float side_opposite = float3_dot(n, float3_subtract(opposite, a));
	float side_p = float3_dot(n, float3_subtract(p, a));
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
			float dist = float3_length_sq(float3_subtract(q, p));
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
	bool result = false;

	Plane plane = plane_from_triangle(t);
	result = equalf(float3_dot(plane.normal, p), plane.distance);

	if (result) {
		float3 a = float3_subtract(t.a, p);
		float3 b = float3_subtract(t.b, p);
		float3 c = float3_subtract(t.c, p);

		float3 norm_pab = float3_cross(a, b);
		float3 norm_pbc = float3_cross(b, c);
		float3 norm_pca = float3_cross(c, a);

		result = float3_dot(norm_pab, norm_pbc) >= 0.0f;
		if (result)
			result = float3_dot(norm_pbc, norm_pca) >= 0.0f;
	}

	return result;
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

	float3 a = simplex->points[1];
	float3 b = simplex->points[0];

	float3 ab = float3_subtract(b, a);
	float3 ao = float3_subtract(origin, a);

	float3 result = { 0 };
	if (float3_dot(ab, ao) > 0.0f) {
		*direction = float3_cross(float3_cross(ab, ao), ab);
		float t = float3_dot(ao, ab) / float3_dot(ab, ab);
		result = float3_add(a, float3_scale(ab, t));
	} else {
		simplex_update(simplex, array_arg(uint32_t, 1));
		*direction = ao;
		result = a;
	}

	return result;
}

float3 simplex_triangle(Simplex3 *simplex, float3 *direction) {
	float3 origin = { 0 };

	float3 a = simplex->points[2];
	float3 b = simplex->points[1];
	float3 c = simplex->points[0];

	float3 ab = float3_subtract(b, a);
	float3 ac = float3_subtract(c, a);
	float3 ao = float3_subtract(origin, a);

	float3 abc = float3_cross(ab, ac);
	float abc_len_sq = float3_length_sq(abc);

	if (abc_len_sq < EPSILON * EPSILON) {
		// vertex a
		float3 closest_point = a;
		float best_dist_sq = float3_length_sq(a);
		Simplex3 new_simplex = *simplex;
		simplex_update(&new_simplex, array_arg(uint32_t, 2));

		// vertex b
		float b_len_sq = float3_length_sq(b);
		if (b_len_sq < best_dist_sq) {
			new_simplex = *simplex;
			simplex_update(&new_simplex, array_arg(uint32_t, 1));
			closest_point = b;
			best_dist_sq = b_len_sq;
		}

		// vertex c
		float c_len_sq = float3_length_sq(c);
		if (c_len_sq < best_dist_sq) {
			new_simplex = *simplex;
			simplex_update(&new_simplex, array_arg(uint32_t, 0));
			closest_point = c;
			best_dist_sq = c_len_sq;
		}

		// edge ab
		float ab_len_sq = float3_length_sq(ab);
		if (ab_len_sq > EPSILON * EPSILON) {
			float t = clampf(-float3_dot(a, ab) / ab_len_sq, 0.0f, 1.0f);
			float3 q = float3_add(a, float3_scale(ab, t));
			float dist_sq = float3_length_sq(q);
			if (dist_sq < best_dist_sq) {
				new_simplex = *simplex;
				simplex_update(&new_simplex, array_arg(uint32_t, 1, 2));
				closest_point = q;
				best_dist_sq = dist_sq;
			}
		}

		// edge ac
		float ac_len_sq = float3_length_sq(ac);
		if (ac_len_sq > EPSILON * EPSILON) {
			float v = clampf(-float3_dot(a, ac) / ac_len_sq, 0.0f, 1.0f);
			float3 q = float3_add(a, float3_scale(ac, v));
			float dist_sq = float3_length_sq(q);
			if (dist_sq < best_dist_sq) {
				new_simplex = *simplex;
				simplex_update(&new_simplex, array_arg(uint32_t, 0, 2));
				closest_point = q;
				best_dist_sq = dist_sq;
			}
		}

		// edge bc
		float3 bc = float3_subtract(c, b);
		float bc_len_sq = float3_length_sq(bc);
		if (bc_len_sq > EPSILON * EPSILON) {
			float v = clampf(-float3_dot(b, bc) / bc_len_sq, 0.0f, 1.0f);
			float3 q = float3_add(b, float3_scale(bc, v));
			float dist_sq = float3_length_sq(q);
			if (dist_sq < best_dist_sq) {
				new_simplex = *simplex;
				simplex_update(&new_simplex, array_arg(uint32_t, 0, 1));
				closest_point = q;
				best_dist_sq = dist_sq;
			}
		}

		if (best_dist_sq > EPSILON * EPSILON) {
			*direction = float3_scale(closest_point, -1.0f);
		} else {
			*direction = (float3){ 0.0f, 0.0f, 0.0f };
		}
		*simplex = new_simplex;
		return closest_point;
	}

	float3 result = { 0 };
	if (float3_dot(float3_cross(abc, ac), ao) > 0.0f) {
		if (float3_dot(ac, ao) > 0.0f) {
			simplex_update(simplex, array_arg(uint32_t, 0, 2));
			*direction = float3_cross(float3_cross(ac, ao), ac);

			float t = float3_dot(ao, ac) / float3_dot(ac, ac);
			result = float3_add(a, float3_scale(ac, t));
		} else {
			simplex_update(simplex, array_arg(uint32_t, 1, 2));
			result = simplex_line(simplex, direction);
		}
	} else {
		if (float3_dot(float3_cross(ab, abc), ao) > 0.0f) {
			simplex_update(simplex, array_arg(uint32_t, 1, 2));
			result = simplex_line(simplex, direction);
		} else {
			if (float3_dot(abc, ao) > 0.0f) {
				*direction = abc;
				result = triangle3_closest_point((Triangle3){ a, b, c }, origin);
			} else {
				simplex_update(simplex, array_arg(uint32_t, 1, 0, 2));
				*direction = float3_scale(abc, -1.0f);
				result = triangle3_closest_point((Triangle3){ b, c, a }, origin);
			}
			/* result = float3_subtract(origin, float3_scale(abc, float3_dot(ao, abc) / float3_dot(abc, abc))); */
		}
	}

	return result;
}

float3 simplex_tetrahedron(Simplex3 *simplex, float3 *direction) {
	float3 origin = { 0 };

	float3 a = simplex->points[3];
	float3 b = simplex->points[2];
	float3 c = simplex->points[1];
	float3 d = simplex->points[0];

	float3 ab = float3_subtract(b, a);
	float3 ac = float3_subtract(c, a);
	float3 ad = float3_subtract(d, a);
	float3 ao = float3_subtract(origin, a);

	float3 abc = float3_cross(ab, ac);
	float3 acd = float3_cross(ac, ad);
	float3 adb = float3_cross(ad, ab);

	bool abc_direction = float3_dot(abc, ao) > 0.0f;
	bool acd_direction = float3_dot(acd, ao) > 0.0f;
	bool adb_direction = float3_dot(adb, ao) > 0.0f;

	if (abc_direction == false && acd_direction == false && adb_direction == false)
		return origin;

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

	float abc_d2 = float3_dot(p_abc, p_abc);
	float acd_d2 = float3_dot(p_acd, p_acd);
	float adb_d2 = float3_dot(p_adb, p_adb);

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
	float3 support = float3_subtract(shape3_support(a, (float3){ 1.0f, 0.0f, 0.0f }), shape3_support(b, (float3){ -1.0f, 0.0f, 0.0f }));

	Simplex3 simplex = { 0 };
	simplex.points[simplex.point_count++] = support;

	float3 direction = float3_scale(support, -1.0f);
	if (equalf(float3_length_sq(direction), 0.0f))
		return 0.0f;

	float3 origin = { 0 };

	float previous_length_sq = FLOAT_MAX;
	float tolerence_sq = 0.001 * 0.001;

	uint32_t loop_count = 0;
	while (true) {
		float3 sa = shape3_support(a, direction);
		float3 sb = shape3_support(b, float3_scale(direction, -1.0f));
		float3 support = float3_subtract(sa, sb);

		simplex.support_a[simplex.point_count] = sa;
		simplex.support_b[simplex.point_count] = sb;
		simplex.points[simplex.point_count++] = support;

		/* float proj_new = float3_dot(support, direction); */
		/* float proj_old = float3_dot(simplex.points[0], direction); */

		/* const float progress_epsilon = 1e-5f; */
		/* if ((proj_new - proj_old) <= progress_epsilon) { */
		/* 	break; */
		/* } */

		uint32_t before_count = simplex.point_count;
		float3 proj;
		if (simplex.point_count == 2) // line
			proj = simplex_line(&simplex, &direction);
		else if (simplex.point_count == 3) // triangle
			proj = simplex_triangle(&simplex, &direction);
		else if (simplex.point_count == 4) // tetrahedron
			proj = simplex_tetrahedron(&simplex, &direction);
		else {
			ASSERT(!"Invalid state for 2D/3D GJK");
		}

		float len_sq = float3_length_sq(proj);
		if (equalf(float3_length_sq(direction), 0.0f)) {
			previous_length_sq = len_sq;

			float error = fabsf(reference_dist_sq - len_sq);
			if (equalf(reference_dist_sq, len_sq) == false && error > max_error) {
				max_error = error;
				LOG_INFO("direciton == 0.0f - error: %g, actual: %g, result: %g, loop_count = %d", max_error, reference_dist_sq, previous_length_sq, loop_count);
			}

			break;
		}

		if (len_sq < previous_length_sq)
			previous_length_sq = len_sq;
		else {
			float error = fabsf(reference_dist_sq - previous_length_sq);
			if (equalf(reference_dist_sq, previous_length_sq) == false && error > max_error) {
				max_error = error;
				LOG_INFO("len_sq < previous_length_sq - error: %g, actual: %g, result: %g, prior_count = %d, new_count = %d", max_error, reference_dist_sq, previous_length_sq, before_count, simplex.point_count);
			}
			break;
		}

		if (len_sq < tolerence_sq) {
			float error = fabsf(reference_dist_sq - previous_length_sq);
			if (equalf(reference_dist_sq, previous_length_sq) == false && error > max_error) {
				max_error = error;
				LOG_INFO("len_sq < tolerence_sq - error: %g, actual: %g, result: %g, prior_count = %d, new_count = %d", max_error, reference_dist_sq, previous_length_sq, before_count, simplex.point_count);
			}
			return 0.0f;
		}

		loop_count += 1;
	}
	ASSERT(equalf(max_error, FLOAT_MAX) == false);

	return previous_length_sq;
}
