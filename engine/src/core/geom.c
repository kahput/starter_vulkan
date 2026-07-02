#include "geom.h"
#include "core/cmath.h"

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

Raycast3Result sphere_sweep_triangle3(float3 origin, float3 direction, float max_distance, Triangle3 triangle) {
	Raycast3Result result = RAY3_NO_HIT;
	result.t = 1.0f;

	bool embedded = false;

	Plane triangle_plane = plane_from_triangle(triangle);
	float3 velocity = float3_scale(direction, max_distance);

	if (float3_dot(triangle_plane.normal, direction) < 0.0f) { // front facing

		float t0, t1;

		float signed_distance = plane_signed_distance(triangle_plane, origin);
		float ndotv = float3_dot(triangle_plane.normal, velocity);

		if (equalf(ndotv, 0.0f)) {
			if (fabsf(signed_distance) >= 1.0f) {
				goto exit_early;
			} else {
				embedded = true;
			}
		} else {
			t0 = (-1.0f - signed_distance) / ndotv;
			t1 = (1.0f - signed_distance) / ndotv;

			if (t0 > t1) {
				float temp = t0;
				t0 = t1;
				t1 = temp;
			}

			if (t0 > 1.0f || t1 < 0.0f) {
				goto exit_early;
			} else {
				t0 = clampf(t0, 0.0f, 1.0f);
				t1 = clampf(t1, 0.0f, 1.0f);
			}
		}

		if (embedded == false) {
			float3 plane_contact_point =
				float3_add(float3_subtract(origin, triangle_plane.normal), float3_scale(velocity, t0));

			if (triangle3_contains_point(triangle, plane_contact_point)) {
				result.hit = true;
				result.t = t0;
				result.point = plane_contact_point;
				result.normal = float3_normalize_safe(triangle_plane.normal, EPSILON);
			}
		}

		if (result.hit == false) {
			float3 base = origin;
			float velocity_length_sq = float3_dot(velocity, velocity);

			float a, b, c;
			float new_t;

			// a *t^2 + b*t + c = 0

			a = velocity_length_sq;

			b = 2.0 * float3_dot(velocity, float3_subtract(base, triangle.a));
			c = float3_length_sq(float3_subtract(triangle.a, base)) - 1.0f;
			if (lowest_root(a, b, c, result.t, &new_t)) {
				result.hit = true;
				result.t = new_t;
				result.point = triangle.a;
				float3 sphere_center_at_t = float3_add(base, float3_scale(velocity, result.t));
				result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
			}

			b = 2.0 * float3_dot(velocity, float3_subtract(base, triangle.b));
			c = float3_length_sq(float3_subtract(triangle.b, base)) - 1.0f;
			if (lowest_root(a, b, c, result.t, &new_t)) {
				result.hit = true;
				result.t = new_t;
				result.point = triangle.b;
				float3 sphere_center_at_t = float3_add(base, float3_scale(velocity, result.t));
				result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
			}

			b = 2.0 * float3_dot(velocity, float3_subtract(base, triangle.c));
			c = float3_length_sq(float3_subtract(triangle.c, base)) - 1.0f;
			if (lowest_root(a, b, c, result.t, &new_t)) {
				result.hit = true;
				result.t = new_t;
				result.point = triangle.c;
				float3 sphere_center_at_t = float3_add(base, float3_scale(velocity, result.t));
				result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
			}

			float3 edge = float3_subtract(triangle.b, triangle.a);
			float3 base_to_vertex = float3_subtract(triangle.a, base);

			float edge_length_sq = float3_length_sq(edge);
			float edge_dot_velocity = float3_dot(edge, velocity);
			float edge_dot_base_to_vertex = float3_dot(edge, base_to_vertex);

			a = edge_length_sq * -velocity_length_sq + (edge_dot_velocity * edge_dot_velocity);
			b = edge_length_sq * (2.0f * float3_dot(velocity, base_to_vertex)) - 2.0 * edge_dot_velocity * edge_dot_base_to_vertex;
			c = edge_length_sq * (1.0f - float3_length_sq(base_to_vertex)) + (edge_dot_base_to_vertex * edge_dot_base_to_vertex);

			if (lowest_root(a, b, c, result.t, &new_t)) {
				float f = (edge_dot_velocity * new_t - edge_dot_base_to_vertex) / edge_length_sq;
				if (f >= 0.0f && f <= 1.0) {
					result.hit = true;
					result.t = new_t;
					result.point = float3_add(triangle.a, float3_scale(edge, f));
					float3 sphere_center_at_t = float3_add(base, float3_scale(velocity, result.t));
					result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
				}
			}

			edge = float3_subtract(triangle.c, triangle.b);
			base_to_vertex = float3_subtract(triangle.b, base);

			edge_length_sq = float3_length_sq(edge);
			edge_dot_velocity = float3_dot(edge, velocity);
			edge_dot_base_to_vertex = float3_dot(edge, base_to_vertex);

			a = edge_length_sq * -velocity_length_sq + (edge_dot_velocity * edge_dot_velocity);
			b = edge_length_sq * (2.0f * float3_dot(velocity, base_to_vertex)) - 2.0 * edge_dot_velocity * edge_dot_base_to_vertex;
			c = edge_length_sq * (1.0f - float3_length_sq(base_to_vertex)) + (edge_dot_base_to_vertex * edge_dot_base_to_vertex);

			if (lowest_root(a, b, c, result.t, &new_t)) {
				float f = (edge_dot_velocity * new_t - edge_dot_base_to_vertex) / edge_length_sq;
				if (f >= 0.0f && f <= 1.0) {
					result.hit = true;
					result.t = new_t;
					result.point = float3_add(triangle.b, float3_scale(edge, f));
					float3 sphere_center_at_t = float3_add(base, float3_scale(velocity, result.t));
					result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
				}
			}

			edge = float3_subtract(triangle.a, triangle.c);
			base_to_vertex = float3_subtract(triangle.c, base);

			edge_length_sq = float3_length_sq(edge);
			edge_dot_velocity = float3_dot(edge, velocity);
			edge_dot_base_to_vertex = float3_dot(edge, base_to_vertex);

			a = edge_length_sq * -velocity_length_sq + (edge_dot_velocity * edge_dot_velocity);
			b = edge_length_sq * (2.0f * float3_dot(velocity, base_to_vertex)) - 2.0 * edge_dot_velocity * edge_dot_base_to_vertex;
			c = edge_length_sq * (1.0f - float3_length_sq(base_to_vertex)) + (edge_dot_base_to_vertex * edge_dot_base_to_vertex);

			if (lowest_root(a, b, c, result.t, &new_t)) {
				float f = (edge_dot_velocity * new_t - edge_dot_base_to_vertex) / edge_length_sq;
				if (f >= 0.0f && f <= 1.0) {
					result.hit = true;
					result.t = new_t;
					result.point = float3_add(triangle.c, float3_scale(edge, f));
					float3 sphere_center_at_t = float3_add(base, float3_scale(velocity, result.t));
					result.normal = float3_normalize(float3_subtract(sphere_center_at_t, result.point));
				}
			}
		}
	}

exit_early: {}
	return result;
}

float distance_point_plane(float3 point, float3 po, float3 pn) {
	pn = float3_normalize_safe(pn, EPSILON);
	return float3_dot(float3_subtract(point, po), pn);
}

float distance_point_segment_squared(float3 point, float3 a, float3 b) {
	float3 ab = float3_subtract(b, a);
	float3 ap = float3_subtract(point, a);

	float t = float3_dot(ap, ab);
	if (t <= 0.0f)
		return float3_dot(ap, ap);

	float length_squared = float3_dot(ab, ab);
	if (t >= length_squared) {
		float3 bp = float3_subtract(point, b);
		return float3_dot(bp, bp);
	}

	return float3_dot(ap, ap) - (t * t) / length_squared;
}

// R = R - (n dot (R - P))n = R = R - tn
float3 plane_closest_point(Plane p, float3 to) {
	float t = (float3_dot(to, p.normal) - p.distance) / float3_length_sq(p.normal);
	return float3_subtract(to, float3_scale(p.normal, t));
}

float3 segment3_closest_point(float3 a, float3 b, float3 to) {
	float3 ab = float3_subtract(b, a);
	float lenght_sq = float3_length_sq(ab);
	if (lenght_sq < EPSILON)
		return a;

	float t = float3_dot(float3_subtract(to, a), ab) / lenght_sq;
	t = clampf(t, 0.0f, 1.0f);

	return float3_add(a, float3_scale(ab, t)); // a + t * ab
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
