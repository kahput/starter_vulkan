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

Raycast3Result raycast_aabb3(float3 ro, float3 rd, float3 center, float3 extent) {
	float3 min = float3_subtract(center, extent);
	float3 max = float3_add(center, extent);

	Raycast3Result result = RAY3_NO_HIT, temp = RAY3_NO_HIT;
	if (float3_length(rd)) {
		// left
		float3 po = float3_subtract(center, (float3){ extent.x, 0.0f, 0.0f });
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
		po = float3_add(center, (float3){ extent.x, 0.0f, 0.0f });
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
		po = float3_add(center, (float3){ 0.0f, 0.0f, extent.z });
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
		po = float3_subtract(center, (float3){ 0.0f, 0.0f, extent.z });
		pn = (float3){ 0.0f, 0.0f, -1.0f };

		temp = raycast_plane(ro, rd, po, pn);
		if ((temp.point.x < min.x || temp.point.x > max.x) ||
			(temp.point.y < min.y || temp.point.y > max.y) ||
			(temp.point.z < min.z || temp.point.z > max.z)) {
			temp = RAY3_NO_HIT;
		}
		if (temp.t < result.t)
			result = temp;

		// NOTE: no vertical movement, ignoring top and bottom for now
	}

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
float3 closest_point_on_plane3(float3 point, float3 po, float3 pn) {
	float t = float3_dot(float3_subtract(point, po), pn) / float3_dot(pn, pn);
	return float3_subtract(point, float3_scale(pn, t));
}

float3 closest_point_on_segment3(float3 point, float3 a, float3 b) {
	float3 ab = float3_subtract(b, a);

	float t = float3_dot(float3_subtract(point, a), ab) / float3_dot(ab, ab);
	t = clampf(t, 0.0f, 1.0f);

	return float3_add(a, float3_scale(ab, t)); // a + t * ab
}
