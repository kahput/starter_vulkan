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
