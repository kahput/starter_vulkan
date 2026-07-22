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

/* Right Hand, Rodrigues' rotation formula:
	v = v*cos(t) + (kxv)sin(t) + k*(k.v)(1 - cos(t))
*/
float3 rotate3(float3 v, float angle, float3 axis) {
	float c = cosf(angle);
	float s = sinf(angle);
	float3 k = norm3(axis);

	return add3(
		scale3(v, c),
		add3(
			scale3(cross3(k, v), s),
			scale3(k, dot3(k, v) * (1.0f - c))));
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

	float length = len3(axis);
	if (length > EPSILON) {
		angle *= 0.5f;
		axis = scale3(axis, 1.0f / length);

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

float2x2 make2x2_from_rotation(float rad) {
	float2x2 result = { 0 };
	float c = cosf(rad);
	float s = sinf(rad);

	result.elements[0] = c;
	result.elements[1] = s;
	result.elements[2] = -s;
	result.elements[3] = c;

	return result;
}

bool equal4x4(float4x4 lhs, float4x4 rhs) {
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

float4x4 identity4x4(void) {
	float4x4 result = { { 1.0f, 0.0f, 0.0f, 0.0f,
	  0.0f, 1.0f, 0.0f, 0.0f,
	  0.0f, 0.0f, 1.0f, 0.0f,
	  0.0f, 0.0f, 0.0f, 1.0f } };

	return result;
}

float4x4 mul4x4(float4x4 lhs, float4x4 rhs) {
	float4x4 result = { 0 };

#define DOT(row, col)                                       \
	(lhs.elements[0 + row] * rhs.elements[col * 4 + 0] +    \
		lhs.elements[4 + row] * rhs.elements[col * 4 + 1] + \
		lhs.elements[8 + row] * rhs.elements[col * 4 + 2] + \
		lhs.elements[12 + row] * rhs.elements[col * 4 + 3])

	result.elements[0] = DOT(0, 0);
	result.elements[4] = DOT(0, 1);
	result.elements[8] = DOT(0, 2);
	result.elements[12] = DOT(0, 3);

	result.elements[1] = DOT(1, 0);
	result.elements[5] = DOT(1, 1);
	result.elements[9] = DOT(1, 2);
	result.elements[13] = DOT(1, 3);

	result.elements[2] = DOT(2, 0);
	result.elements[6] = DOT(2, 1);
	result.elements[10] = DOT(2, 2);
	result.elements[14] = DOT(2, 3);

	result.elements[3] = DOT(3, 0);
	result.elements[7] = DOT(3, 1);
	result.elements[11] = DOT(3, 2);
	result.elements[15] = DOT(3, 3);

#undef DOT

	return result;
}

float4 mul4x4v(float4x4 m, float4 v) {
	float4 result = { 0 };

	result.x = m.elements[0] * v.x + m.elements[4] * v.y + m.elements[8] * v.z + m.elements[12] * v.w;
	result.y = m.elements[1] * v.x + m.elements[5] * v.y + m.elements[9] * v.z + m.elements[13] * v.w;
	result.z = m.elements[2] * v.x + m.elements[6] * v.y + m.elements[10] * v.z + m.elements[14] * v.w;
	result.w = m.elements[3] * v.x + m.elements[7] * v.y + m.elements[11] * v.z + m.elements[15] * v.w;

	return result;
}

float4x4 translate4x4(float4x4 m, float3 t) {
	float4x4 result = m;

	result.elements[12] = m.elements[0] * t.x + m.elements[4] * t.y + m.elements[8] * t.z + m.elements[12];
	result.elements[13] = m.elements[1] * t.x + m.elements[5] * t.y + m.elements[9] * t.z + m.elements[13];
	result.elements[14] = m.elements[2] * t.x + m.elements[6] * t.y + m.elements[10] * t.z + m.elements[14];
	result.elements[15] = m.elements[3] * t.x + m.elements[7] * t.y + m.elements[11] * t.z + m.elements[15];

	return result;
}

float4x4 scale4x4(float4x4 m, float3 s) {
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

float4x4 rotate4x4(float4x4 m, float angle, float3 axis) {
	// Post-multiply by rotation: result = m * R
	// Means: each of the first 3 columns of m gets mixed by R.
	float4x4 r = make4x4_from_rotation(axis, angle);
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

float4x4 make4x4_from_translation(float3 v) {
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

float4x4 make4x4_from_rotation(float3 axis, float angle) {
	float c = cosf(angle);
	float s = sinf(angle);
	float t = 1.0f - c;

	float3 normalized_axis = norm3(axis);
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

float4x4 make4x4_from_scale(float3 scale) {
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

float4x4 make4x4_from_quat(quat4 q) {
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

float4x4 transpose4x4(float4x4 m) {
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

float4x4 compose4x4_from_euler(float3 position, float3 rotation, float3 scale) {
	float4x4 result = { 0 };

	float4x4 T = make4x4_from_translation(position);
	float4x4 S = make4x4_from_scale(scale);
	float4x4 rotation_x = make4x4_from_rotation(unit3(RIGHT), rotation.x);
	float4x4 rotation_y = make4x4_from_rotation(unit3(UP), rotation.y);
	float4x4 rotation_z = make4x4_from_rotation(unit3(FORWARD), rotation.z);
	float4x4 R = mul4x4(rotation_z, mul4x4(rotation_y, rotation_x));

	result = mul4x4(T, mul4x4(R, S));
	return result;
}

float4x4 compose4x4_from_quat(float3 position, quat4 rotation, float3 scale) {
	float4x4 result = { 0 };
	float4x4 T = make4x4_from_translation(position);
	float4x4 S = make4x4_from_scale(scale);
	float4x4 R = make4x4_from_quat(rotation);
	result = mul4x4(T, mul4x4(R, S));
	return result;
}

float4x4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
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

float4x4 orthographic(float left, float right, float bottom, float top, float near, float far) {
	float4x4 result = identity4x4();

	result.elements[0] = 2.0f / (right - left);
	result.elements[5] = 2.0f / (top - bottom);
	result.elements[10] = -1.0f / (far - near);

	result.elements[12] = -(right + left) / (right - left);
	result.elements[13] = -(top + bottom) / (top - bottom);
	result.elements[14] = -(near) / (far - near);
	result.elements[15] = 1.0f;

	return result;
}

float4x4 lookat(float3 eye, float3 center, float3 up) {
	float4x4 result = identity4x4();

	float3 f = norm3(sub3(center, eye));
	float3 r = norm3(cross3(f, up));
	float3 u = cross3(r, f);

	// Row 0: Right Vector (s)
	result.elements[0] = r.x; // Col 0
	result.elements[4] = r.y; // Col 1
	result.elements[8] = r.z; // Col 2
	result.elements[12] = -dot3(r, eye); // Translation X

	// Row 1: Up Vector (u)
	result.elements[1] = u.x; // Col 0
	result.elements[5] = u.y; // Col 1
	result.elements[9] = u.z; // Col 2
	result.elements[13] = -dot3(u, eye); // Translation Y

	result.elements[2] = -f.x; // Col 0
	result.elements[6] = -f.y; // Col 1
	result.elements[10] = -f.z; // Col 2
	result.elements[14] = dot3(f, eye); // Translation Z (-dot(-f, eye))

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
