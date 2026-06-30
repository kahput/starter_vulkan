#include "cmath.h"
#include "core/logger.h"
#include <stdlib.h>

float randf_range(float min, float max) {
	return min + ((float)rand() / RAND_MAX) * (max - min);
}
uint32_t randu_range(uint32_t min, uint32_t max) {
	return min + (rand() % (max - min + 1));
}
int32_t randi_range(int32_t min, int32_t max) {
	return min + (rand() % (max - min + 1));
}

bool float2_equal(float2 a, float2 b) {
	bool result = equalf(a.x, b.x) && equalf(a.y, b.y);

	return result;
}

float2 float2_negate(float2 v) {
	float2 result = { -v.x, -v.y };

	return result;
}

float float2_length_sq(float2 v) {
	float result = v.x * v.x + v.y * v.y;

	return result;
}

float float2_length(float2 v) {
	float result = sqrt(v.x * v.x + v.y * v.y);

	return result;
}
float2 float2_normalize(float2 v) {
	float length = float2_length(v);
	float2 result = { .x = v.x / length, .y = v.y / length };

	return result;
}

float2 float2_normalize_safe(float2 v, float epsilon) {
	float length = float2_length(v);
	if (length < EPSILON) {
		return (float2){ 0 };
	}

	float2 result = { .x = v.x / length, .y = v.y / length };
	return result;
}

float2 float2_add(float2 a, float2 b) {
	float2 result = { a.x + b.x, a.y + b.y };

	return result;
}
float2 float2_subtract(float2 a, float2 b) {
	float2 result = { a.x - b.x, a.y - b.y };

	return result;
}

float2 float2_divide(float2 a, float2 b) {
	float2 result = { a.x / b.x, a.y / b.y };

	return result;
}

float2 float2_scale(float2 v, float s) {
	float2 result = { v.x * s, v.y * s };

	return result;
}

float float2_dot(float2 a, float2 b) {
	float result = a.x * b.x + a.y * b.y;

	return result;
}

float3 float3_add(float3 a, float3 b) {
	float3 result = { a.x + b.x, a.y + b.y, a.z + b.z };

	return result;
}
float3 float3_subtract(float3 a, float3 b) {
	float3 result = { a.x - b.x, a.y - b.y, a.z - b.z };

	return result;
}
float3 float3_scale(float3 v, float s) {
	float3 result = { v.x * s, v.y * s, v.z * s };

	return result;
}

bool float3_equal(float3 a, float3 b) {
	bool result = equalf(a.x, b.x) && equalf(a.y, b.y) && equalf(a.z, b.z);

	return result;
}

float3 float3_negate(float3 v) { return (float3){ -v.x, -v.y, -v.z }; }

float float3_dot(float3 a, float3 b) {
	float result = a.x * b.x + a.y * b.y + a.z * b.z;

	return result;
}

float3 float3_cross(float3 a, float3 b) {
	float3 result = {
		.x = a.y * b.z - b.y * a.z,
		.y = a.z * b.x - b.z * a.x,
		.z = a.x * b.y - b.x * a.y,
	};

	return result;
}

float float3_length_sq(float3 v) {
	float result = float3_dot(v, v);

	return result;
}

float float3_length(float3 v) {
	float result = sqrtf(float3_dot(v, v));

	return result;
}

float3 float3_normalize(float3 v) {
	float3 result = float3_scale(v, 1 / float3_length(v));

	return result;
}
float3 float3_normalize_safe(float3 v, float epsilon) {
	float length = float3_length(v);
	if (length < epsilon)
		return (float3){ 0 };

	return float3_scale(v, 1.0f / length);
}

float3 float3_min(float3 a, float3 b) {
	float3 result = {
		.x = minf(a.x, b.x),
		.y = minf(a.y, b.y),
		.z = minf(a.z, b.z),
	};

	return result;
}
float3 float3_max(float3 a, float3 b) {
	float3 result = {
		.x = maxf(a.x, b.x),
		.y = maxf(a.y, b.y),
		.z = maxf(a.z, b.z),
	};

	return result;
}

float3 float3_lerp(float3 start, float3 end, float amount) {
	float3 result = { 0 };

	result.x = start.x + amount * (end.x - start.x);
	result.y = start.y + amount * (end.y - start.y);
	result.z = start.z + amount * (end.z - start.z);

	return result;
}

float float3_angle(float3 a, float3 b) {
	float dot = float3_dot(float3_normalize_safe(a, EPSILON), float3_normalize_safe(b, EPSILON));

	if (dot > 1.0f)
		dot = 1.0f;
	if (dot < -1.0f)
		dot = -1.0f;

	return acosf(dot);
}

/* Right Hand, Rodrigues' rotation formula:
	v = v*cos(t) + (kxv)sin(t) + k*(k.v)(1 - cos(t))
*/
float3 float3_rotate(float3 v, float angle, float3 axis) {
	float c = cosf(angle);
	float s = sinf(angle);
	float3 k = float3_normalize_safe(axis, EPSILON);

	return float3_add(
		float3_scale(v, c),
		float3_add(
			float3_scale(float3_cross(k, v), s),
			float3_scale(k, float3_dot(k, v) * (1.0f - c))));
}

bool float4_equal(float4 a, float4 b) {
	bool result = equalf(a.x, b.x) && equalf(a.y, b.y) && equalf(a.z, b.z) && equalf(a.w, b.w);

	return result;
}

float float4_length(float4 v) {
	float result = sqrtf(float4_dot(v, v));

	return result;
}

float3 quat4_to_euler(quat4 q) {
	float3 result = { 0 };

	// Roll (x-axis rotation)
	float x0 = 2.0f * (q.w * q.x + q.y * q.z);
	float x1 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
	result.x = atan2f(x0, x1);

	// Pitch (y-axis rotation)
	float y0 = 2.0f * (q.w * q.y - q.z * q.x);
	y0 = y0 > 1.0f ? 1.0f : y0;
	y0 = y0 < -1.0f ? -1.0f : y0;
	result.y = asinf(y0);

	// Yaw (z-axis rotation)
	float z0 = 2.0f * (q.w * q.z + q.x * q.y);
	float z1 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
	result.z = atan2f(z0, z1);

	return result;
}

quat4 quat4_from_axis_angle(float3 axis, float angle) {
	quat4 result = { 0.0f, 0.0f, 0.0f, 1.0f };

	float length = float3_length(axis);
	if (length > EPSILON) {
		angle *= 0.5f;
		axis = float3_scale(axis, 1.0f / length);

		float s = sinf(angle);
		float c = cosf(angle);

		result.x = axis.x * s;
		result.y = axis.y * s;
		result.z = axis.z * s;
		result.w = c;

		// NOTE: Maybe normalize?
		/* result = float4_scale(result, 1 / float4_length(result)); */
	}

	return result;
}

quat4 quat4_slerp(quat4 q, quat4 p, float t) {
	quat4 result = { 0 };

	float cosHalfTheta = q.x * p.x + q.y * p.y + q.z * p.z + q.w * p.w;

	if (cosHalfTheta < 0) {
		p.x = -p.x;
		p.y = -p.y;
		p.z = -p.z;
		p.w = -p.w;
		cosHalfTheta = -cosHalfTheta;
	}

	if (fabsf(cosHalfTheta) >= 1.0f)
		result = q;
	else {
		float halfTheta = acosf(cosHalfTheta);
		float sinHalfTheta = sqrtf(1.0f - cosHalfTheta * cosHalfTheta);

		if (fabsf(sinHalfTheta) < EPSILON) {
			result.x = q.x * (1.0f - t) + p.x * t;
			result.y = q.y * (1.0f - t) + p.y * t;
			result.z = q.z * (1.0f - t) + p.z * t;
			result.w = q.w * (1.0f - t) + p.w * t;
		} else {
			float ratioA = sinf((1 - t) * halfTheta) / sinHalfTheta;
			float ratioB = sinf(t * halfTheta) / sinHalfTheta;

			result.x = (q.x * ratioA + p.x * ratioB);
			result.y = (q.y * ratioA + p.y * ratioB);
			result.z = (q.z * ratioA + p.z * ratioB);
			result.w = (q.w * ratioA + p.w * ratioB);
		}
	}

	return result;
}

bool float4x4_equal(float4x4 lhs, float4x4 rhs) {
	return equalf(lhs.elements[0], rhs.elements[0]) &&
		equalf(lhs.elements[1], rhs.elements[1]) &&
		equalf(lhs.elements[2], rhs.elements[2]) &&
		equalf(lhs.elements[3], rhs.elements[3]) &&
		equalf(lhs.elements[4], rhs.elements[4]) &&
		equalf(lhs.elements[5], rhs.elements[5]) &&
		equalf(lhs.elements[6], rhs.elements[6]) &&
		equalf(lhs.elements[7], rhs.elements[7]) &&
		equalf(lhs.elements[8], rhs.elements[8]) &&
		equalf(lhs.elements[9], rhs.elements[9]) &&
		equalf(lhs.elements[10], rhs.elements[10]) &&
		equalf(lhs.elements[11], rhs.elements[11]) &&
		equalf(lhs.elements[12], rhs.elements[12]) &&
		equalf(lhs.elements[13], rhs.elements[13]) &&
		equalf(lhs.elements[14], rhs.elements[14]) &&
		equalf(lhs.elements[15], rhs.elements[15]);
}

float4x4 float4x4_identity(void) {
	float4x4 result = { { 1.0f, 0.0f, 0.0f, 0.0f,
	  0.0f, 1.0f, 0.0f, 0.0f,
	  0.0f, 0.0f, 1.0f, 0.0f,
	  0.0f, 0.0f, 0.0f, 1.0f } };

	return result;
}

float4x4 float4x4_multiply(float4x4 lhs, float4x4 rhs) {
	float4x4 result = { 0 };

#define MAT4_DOT(row, col)                                  \
	(lhs.elements[0 + row] * rhs.elements[col * 4 + 0] +    \
		lhs.elements[4 + row] * rhs.elements[col * 4 + 1] + \
		lhs.elements[8 + row] * rhs.elements[col * 4 + 2] + \
		lhs.elements[12 + row] * rhs.elements[col * 4 + 3])

	result.elements[0] = MAT4_DOT(0, 0);
	result.elements[4] = MAT4_DOT(0, 1);
	result.elements[8] = MAT4_DOT(0, 2);
	result.elements[12] = MAT4_DOT(0, 3);

	result.elements[1] = MAT4_DOT(1, 0);
	result.elements[5] = MAT4_DOT(1, 1);
	result.elements[9] = MAT4_DOT(1, 2);
	result.elements[13] = MAT4_DOT(1, 3);

	result.elements[2] = MAT4_DOT(2, 0);
	result.elements[6] = MAT4_DOT(2, 1);
	result.elements[10] = MAT4_DOT(2, 2);
	result.elements[14] = MAT4_DOT(2, 3);

	result.elements[3] = MAT4_DOT(3, 0);
	result.elements[7] = MAT4_DOT(3, 1);
	result.elements[11] = MAT4_DOT(3, 2);
	result.elements[15] = MAT4_DOT(3, 3);

#undef MAT4_DOT

	return result;
}

float4x4 float4x4_translate(float4x4 m, float3 t) {
	float4x4 result = m;

	result.elements[12] = m.elements[0] * t.x + m.elements[4] * t.y + m.elements[8] * t.z + m.elements[12];
	result.elements[13] = m.elements[1] * t.x + m.elements[5] * t.y + m.elements[9] * t.z + m.elements[13];
	result.elements[14] = m.elements[2] * t.x + m.elements[6] * t.y + m.elements[10] * t.z + m.elements[14];
	result.elements[15] = m.elements[3] * t.x + m.elements[7] * t.y + m.elements[11] * t.z + m.elements[15];

	return result;
}

float4x4 float4x4_scale(float4x4 m, float3 s) {
	float4x4 result = m;

	// Post-multiply by scale: result = m * S
	// This scales the first 3 columns of m (the basis vectortors).
	result.elements[0] = m.elements[0] * s.x;
	result.elements[1] = m.elements[1] * s.x;
	result.elements[2] = m.elements[2] * s.x;
	result.elements[3] = m.elements[3] * s.x;

	result.elements[4] = m.elements[4] * s.y;
	result.elements[5] = m.elements[5] * s.y;
	result.elements[6] = m.elements[6] * s.y;
	result.elements[7] = m.elements[7] * s.y;

	result.elements[8] = m.elements[8] * s.z;
	result.elements[9] = m.elements[9] * s.z;
	result.elements[10] = m.elements[10] * s.z;
	result.elements[11] = m.elements[11] * s.z;

	// translation column unchanged
	result.elements[12] = m.elements[12];
	result.elements[13] = m.elements[13];
	result.elements[14] = m.elements[14];
	result.elements[15] = m.elements[15];

	return result;
}

float4x4 float4x4_rotate(float4x4 m, float angle, float3 axis) {
	// Post-multiply by rotation: result = m * R
	// Means: each of the first 3 columns of m gets mixed by R.
	float4x4 r = float4x4_rotation(angle, axis);
	float4x4 result = m;

	// Cache m's basis columns (col 0,1,2). Translation col stays as-is.
	float m0 = m.elements[0], m1 = m.elements[1], m2 = m.elements[2], m3 = m.elements[3];
	float m4 = m.elements[4], m5 = m.elements[5], m6 = m.elements[6], m7 = m.elements[7];
	float m8 = m.elements[8], m9 = m.elements[9], m10 = m.elements[10], m11 = m.elements[11];

	// col0' = m * r.col0
	result.elements[0] = m0 * r.elements[0] + m4 * r.elements[1] + m8 * r.elements[2];
	result.elements[1] = m1 * r.elements[0] + m5 * r.elements[1] + m9 * r.elements[2];
	result.elements[2] = m2 * r.elements[0] + m6 * r.elements[1] + m10 * r.elements[2];
	result.elements[3] = m3 * r.elements[0] + m7 * r.elements[1] + m11 * r.elements[2];

	// col1' = m * r.col1
	result.elements[4] = m0 * r.elements[4] + m4 * r.elements[5] + m8 * r.elements[6];
	result.elements[5] = m1 * r.elements[4] + m5 * r.elements[5] + m9 * r.elements[6];
	result.elements[6] = m2 * r.elements[4] + m6 * r.elements[5] + m10 * r.elements[6];
	result.elements[7] = m3 * r.elements[4] + m7 * r.elements[5] + m11 * r.elements[6];

	// col2' = m * r.col2
	result.elements[8] = m0 * r.elements[8] + m4 * r.elements[9] + m8 * r.elements[10];
	result.elements[9] = m1 * r.elements[8] + m5 * r.elements[9] + m9 * r.elements[10];
	result.elements[10] = m2 * r.elements[8] + m6 * r.elements[9] + m10 * r.elements[10];
	result.elements[11] = m3 * r.elements[8] + m7 * r.elements[9] + m11 * r.elements[10];

	// translation column unchanged for post-multiply by pure rotation
	result.elements[12] = m.elements[12];
	result.elements[13] = m.elements[13];
	result.elements[14] = m.elements[14];
	result.elements[15] = m.elements[15];

	return result;
}

float4x4 float4x4_translation(float3 v) {
	// clang-format off
	float4x4 result = {{
	  [0] = 1.0f, [4] = 0.0f, [8] =  0.0f, [12] = v.x,
	  [1] = 0.0f, [5] = 1.0f, [9] =  0.0f, [13] = v.y,
	  [2] = 0.0f, [6] = 0.0f, [10] = 1.0f, [14] = v.z,
	  [3] = 0.0f, [7] = 0.0f, [11] = 0.0f, [15] = 1.0f
	}};
	// clang-format on

	return result;
}

float4x4 float4x4_rotation(float angle, float3 axis) {
	float c = cosf(angle);
	float s = sinf(angle);
	float t = 1.0f - c;

	float3 normalized_axis = float3_normalize_safe(axis, EPSILON);
	float x = normalized_axis.x;
	float y = normalized_axis.y;
	float z = normalized_axis.z;

	// clang-format off
	float4x4 result = {{
	  [0]  = c + x*x*t,     [4]  = x*y*t - z*s, [8]  = x*z*t + y*s, [12] = 0.0f,
	  [1]  = y*x*t + z*s,   [5]  = y*y*t + c,   [9]  = y*z*t - x*s, [13] = 0.0f,
	  [2]  = z*x*t - y*s,   [6]  = z*y*t + x*s, [10] = c + z*z*t,   [14] = 0.0f,
	  [3]  = 0.0f,          [7]  = 0.0f,        [11] = 0.0f,        [15] = 1.0f
	}};
	// clang-format on

	return result;
}

float4x4 float4x4_scaling(float3 scale) {
	// clang-format off
	float4x4 result = {{
	  [0] = scale.x, [4] = 0.0f,    [8 ] = 0.0f,    [12] = 0.0f,
	  [1] = 0.0f,    [5] = scale.y, [9 ] = 0.0f,    [13] = 0.0f,
	  [2] = 0.0f,    [6] = 0.0f,    [10] = scale.z, [14] = 0.0f,
	  [3] = 0.0f,    [7] = 0.0f,    [11] = 0.0f,    [15] = 1.0f,
	}};
	// clang-format on

	return result;
}

float4x4 float4x4_from_quat(quat4 q) {
	float x = q.x, y = q.y, z = q.z, w = q.w;
	float xx = x * x, yy = y * y, zz = z * z;
	float xy = x * y, xz = x * z, yz = y * z;
	float wx = w * x, wy = w * y, wz = w * z;

	// clang-format off
	float4x4 result = {{
	  [0]  = 1.0f - 2.0f*(yy+zz), [4]  = 2.0f*(xy - wz),       [8]  = 2.0f*(xz + wy),       [12] = 0.0f,
	  [1]  = 2.0f*(xy + wz),      [5]  = 1.0f - 2.0f*(xx+zz),  [9]  = 2.0f*(yz - wx),       [13] = 0.0f,
	  [2]  = 2.0f*(xz - wy),      [6]  = 2.0f*(yz + wx),       [10] = 1.0f - 2.0f*(xx+yy),  [14] = 0.0f,
	  [3]  = 0.0f,                [7]  = 0.0f,                  [11] = 0.0f,                  [15] = 1.0f
	}};
	// clang-format on
	return result;
}

float4x4 float4x4_transpose(float4x4 m) {
	float4x4 result = { 0 };

	result.elements[0] = m.elements[0];
	result.elements[1] = m.elements[4];
	result.elements[2] = m.elements[8];
	result.elements[3] = m.elements[12];

	result.elements[4] = m.elements[1];
	result.elements[5] = m.elements[5];
	result.elements[6] = m.elements[9];
	result.elements[7] = m.elements[13];

	result.elements[8] = m.elements[2];
	result.elements[9] = m.elements[6];
	result.elements[10] = m.elements[10];
	result.elements[11] = m.elements[14];

	result.elements[12] = m.elements[3];
	result.elements[13] = m.elements[7];
	result.elements[14] = m.elements[11];
	result.elements[15] = m.elements[15];

	return result;
}

float4x4 float4x4_compose(float3 position, float3 rotation, float3 scale) {
	float4x4 result = { 0 };

	float4x4 T = float4x4_translation(position);
	float4x4 S = float4x4_scaling(scale);
	float4x4 rotation_x = float4x4_rotation(rotation.x, FLOAT3_X);
	float4x4 rotation_y = float4x4_rotation(rotation.y, FLOAT3_Y);
	float4x4 rotation_z = float4x4_rotation(rotation.z, FLOAT3_Z);
	float4x4 R = float4x4_multiply(rotation_z, float4x4_multiply(rotation_y, rotation_x));

	result = float4x4_multiply(T, float4x4_multiply(R, S));
	return result;
}

float4x4 float4x4_compose_quat(float3 position, quat4 rotation, float3 scale) {
	float4x4 result = { 0 };
	float4x4 T = float4x4_translation(position);
	float4x4 S = float4x4_scaling(scale);
	float4x4 R = float4x4_from_quat(rotation);
	result = float4x4_multiply(T, float4x4_multiply(R, S));
	return result;
}

float3 float4x4_transform(float4x4 m, float4 v) {
	float3 result = {
		m.elements[0] * v.x + m.elements[4] * v.y + m.elements[8] * v.z + m.elements[12] * v.w,
		m.elements[1] * v.x + m.elements[5] * v.y + m.elements[9] * v.z + m.elements[13] * v.w,
		m.elements[2] * v.x + m.elements[6] * v.y + m.elements[10] * v.z + m.elements[14] * v.w,
	};

	return result;
}

float4x4 float4x4_perspective(float fovy_radians, float aspect, float near_z, float far_z) {
	float4x4 result = { 0 };

	float f = 1.0f / tanf(fovy_radians * 0.5f);

	result.elements[0] = f / aspect;
	result.elements[5] = -f;

	// Vulkan NDC z: [0, 1]
	result.elements[10] = far_z / (near_z - far_z);
	result.elements[11] = -1.0f;
	result.elements[14] = (far_z * near_z) / (near_z - far_z);

	return result;
}

float4x4 float4x4_orthographic(float left, float right, float bottom, float top, float near, float far) {
	float4x4 result = float4x4_identity();

	result.elements[0] = 2.0f / (right - left);
	result.elements[5] = 2.0f / (top - bottom);
	result.elements[10] = -1.0f / (far - near);

	result.elements[12] = -(right + left) / (right - left);
	result.elements[13] = -(top + bottom) / (top - bottom);
	result.elements[14] = -(near) / (far - near);
	result.elements[15] = 1.0f;

	return result;
}

float4x4 float4x4_lookat(float3 eye, float3 center, float3 up) {
	float4x4 result = float4x4_identity();

	float3 f = float3_normalize_safe(float3_subtract(center, eye), EPSILON);
	float3 r = float3_normalize_safe(float3_cross(f, up), EPSILON);
	float3 u = float3_cross(r, f);

	// Row 0: Right Vector (s)
	result.elements[0] = r.x; // Col 0
	result.elements[4] = r.y; // Col 1
	result.elements[8] = r.z; // Col 2
	result.elements[12] = -float3_dot(r, eye); // Translation X

	// Row 1: Up Vector (u)
	result.elements[1] = u.x; // Col 0
	result.elements[5] = u.y; // Col 1
	result.elements[9] = u.z; // Col 2
	result.elements[13] = -float3_dot(u, eye); // Translation Y

	result.elements[2] = -f.x; // Col 0
	result.elements[6] = -f.y; // Col 1
	result.elements[10] = -f.z; // Col 2
	result.elements[14] = float3_dot(f, eye); // Translation Z (-dot(-f, eye))

	result.elements[3] = 0.0f;
	result.elements[7] = 0.0f;
	result.elements[11] = 0.0f;
	result.elements[15] = 1.0f;

	return result;
}

void float4x4_print(float4x4 m) {
	LOG_DEBUG(
		"float4x4 {\n"
		" %.2f, %.2f, %.2f, %.2f,\n" // Row 0
		" %.2f, %.2f, %.2f, %.2f,\n" // Row 1
		" %.2f, %.2f, %.2f, %.2f,\n" // Row 2
		" %.2f, %.2f, %.2f, %.2f\n" // Row 3
		"}",
		// Row 0: indices 0, 4, 8, 12
		m.elements[0], m.elements[4], m.elements[8], m.elements[12],
		// Row 1: indices 1, 5, 9, 13
		m.elements[1], m.elements[5], m.elements[9], m.elements[13],
		// Row 2: indices 2, 6, 10, 14
		m.elements[2], m.elements[6], m.elements[10], m.elements[14],
		// Row 3: indices 3, 7, 11, 15
		m.elements[3], m.elements[7], m.elements[11], m.elements[15]);
}

void float2_print(float2 v) {
	LOG_INFO("float2 { %.2f, %.2f, %.2f }", v.x, v.y);
}

void float3_print(float3 v) {
	LOG_INFO("float3 { %.2f, %.2f, %.2f }", v.x, v.y, v.z);
}

void float4_print(float4 v) {
	LOG_INFO("float4 { %.2f, %.2f, %.2f, %.2f }", v.x, v.y, v.z, v.w);
}
