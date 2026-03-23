#include "cmath.h"
#include "core/logger.h"

#include <math.h>

float float2_length(float2 v) {
	return sqrt(v.x * v.x + v.y * v.y);
}
float2 float2_normalize(float2 v) {
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

float float3_length(float3 v) {
	float result = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);

	return result;
}

float3 float3_normalize(float3 v) {
	float3 result = float3_scale(v, 1 / float3_length(v));

	return result;
}
float3 float3_normalize_safe(float3 v, float epsilon) {
	float length = float3_length(v);
	if (length < epsilon) {
		return (float3){ 0 }; // Return zero vectortor if too small
	}
	return float3_scale(v, 1.0f / length);
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

Matrix4f float44_identity(void) {
	Matrix4f result = { { 1.0f, 0.0f, 0.0f, 0.0f,
	  0.0f, 1.0f, 0.0f, 0.0f,
	  0.0f, 0.0f, 1.0f, 0.0f,
	  0.0f, 0.0f, 0.0f, 1.0f } };

	return result;
}

Matrix4f float44_multiply(Matrix4f lhs, Matrix4f rhs) {
	Matrix4f result = { 0 };

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

Matrix4f float44_translate(Matrix4f m, float3 t) {
	Matrix4f result = m;

	result.elements[12] = m.elements[0] * t.x + m.elements[4] * t.y + m.elements[8] * t.z + m.elements[12];
	result.elements[13] = m.elements[1] * t.x + m.elements[5] * t.y + m.elements[9] * t.z + m.elements[13];
	result.elements[14] = m.elements[2] * t.x + m.elements[6] * t.y + m.elements[10] * t.z + m.elements[14];
	result.elements[15] = m.elements[3] * t.x + m.elements[7] * t.y + m.elements[11] * t.z + m.elements[15];

	return result;
}

Matrix4f float44_scale(Matrix4f m, float3 s) {
	Matrix4f result = m;

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

Matrix4f float44_rotate(Matrix4f m, float angle, float3 axis) {
	// Post-multiply by rotation: result = m * R
	// Means: each of the first 3 columns of m gets mixed by R.
	Matrix4f r = float44_rotated(angle, axis);
	Matrix4f result = m;

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

Matrix4f float44_translated(float3 v) {
	// clang-format off
	Matrix4f result = {{
	  [0] = 1.0f, [4] = 0.0f, [8] =  0.0f, [12] = v.x,
	  [1] = 0.0f, [5] = 1.0f, [9] =  0.0f, [13] = v.y,
	  [2] = 0.0f, [6] = 0.0f, [10] = 1.0f, [14] = v.z,
	  [3] = 0.0f, [7] = 0.0f, [11] = 0.0f, [15] = 1.0f
	}};
	// clang-format on

	return result;
}

Matrix4f float44_rotated(float angle, float3 axis) {
	float c = cosf(angle);
	float s = sinf(angle);
	float t = 1.0f - c;

	float3 normalized_axis = float3_normalize_safe(axis, EPSILON);
	float x = normalized_axis.x;
	float y = normalized_axis.y;
	float z = normalized_axis.z;

	// clang-format off
	Matrix4f result = {{
	  [0]  = c + x*x*t,     [4]  = x*y*t - z*s, [8]  = x*z*t + y*s, [12] = 0.0f,
	  [1]  = y*x*t + z*s,   [5]  = y*y*t + c,   [9]  = y*z*t - x*s, [13] = 0.0f,
	  [2]  = z*x*t - y*s,   [6]  = z*y*t + x*s, [10] = c + z*z*t,   [14] = 0.0f,
	  [3]  = 0.0f,          [7]  = 0.0f,        [11] = 0.0f,        [15] = 1.0f
	}};
	// clang-format on

	return result;
}

Matrix4f float44_scaled(float3 scale) {
	// clang-format off
	Matrix4f result = {{
	  [0] = scale.x, [4] = 0.0f,    [8 ] = 0.0f,    [12] = 0.0f,
	  [1] = 0.0f,    [5] = scale.y, [9 ] = 0.0f,    [13] = 0.0f,
	  [2] = 0.0f,    [6] = 0.0f,    [10] = scale.z, [14] = 0.0f,
	  [3] = 0.0f,    [7] = 0.0f,    [11] = 0.0f,    [15] = 1.0f,
	}};
	// clang-format on

	return result;
}

Matrix4f float44_perspective(float fovy_radians, float aspect, float near_z, float far_z) {
	Matrix4f result = { 0 };

	float f = 1.0f / tanf(fovy_radians * 0.5f);

	result.elements[0] = f / aspect;
	result.elements[5] = f;

	// Vulkan NDC z: [0, 1]
	result.elements[10] = far_z / (near_z - far_z);
	result.elements[11] = -1.0f;
	result.elements[14] = (far_z * near_z) / (near_z - far_z);

	return result;
}

Matrix4f float44_orthographic(float left, float right, float bottom, float top, float near, float far) {
	Matrix4f result = float44_identity();

	result.elements[0] = 2.0f / (right - left);
	result.elements[5] = 2.0f / (top - bottom);
	result.elements[10] = -2.0f / (far - near);

	result.elements[12] = -(right + left) / (right - left);
	result.elements[13] = -(top + bottom) / (top - bottom);
	result.elements[14] = -(far + near) / (far - near);
	result.elements[15] = 1.0f;

	return result;
}

Matrix4f float44_lookat(float3 eye, float3 center, float3 up) {
	Matrix4f result = float44_identity();

	// 1. Calculate Basis Vectors
	// Forward (f): Points from eye to center (standard OpenGL/Vulkan convention is -Z forward,
	// so we actually want the vectortor pointing OUT of the screen for the Z-axis basis).
	// Let's stick to standard Right-Handed Rule derivation:
	// f = normalized(center - eye) -> Direction usually labeled "Forward"
	// z_axis = -f                 -> The actual Z column of the matrix
	float3 f = float3_normalize_safe(float3_subtract(center, eye), EPSILON);

	// Right (s)
	float3 s = float3_normalize_safe(float3_cross(f, up), EPSILON);

	// Up (u)
	float3 u = float3_cross(s, f);

	// 2. Fill Matrix
	// The View Matrix Rotation is the Transpose of the Camera orientation.
	// So the Basis Vectors (s, u, -f) become the ROWS of the matrix.

	// Row 0: Right Vector (s)
	result.elements[0] = s.x; // Col 0
	result.elements[4] = s.y; // Col 1
	result.elements[8] = s.z; // Col 2
	result.elements[12] = -float3_dot(s, eye); // Translation X

	// Row 1: Up Vector (u)
	result.elements[1] = u.x; // Col 0
	result.elements[5] = u.y; // Col 1
	result.elements[9] = u.z; // Col 2
	result.elements[13] = -float3_dot(u, eye); // Translation Y

	// Row 2: Back Vector (-f)
	// (Note: we use -f because the camera looks down -Z)
	result.elements[2] = -f.x; // Col 0
	result.elements[6] = -f.y; // Col 1
	result.elements[10] = -f.z; // Col 2
	result.elements[14] = float3_dot(f, eye); // Translation Z (-dot(-f, eye))

	// Row 3: Identity
	result.elements[3] = 0.0f;
	result.elements[7] = 0.0f;
	result.elements[11] = 0.0f;
	result.elements[15] = 1.0f;

	return result;
}

void float44_print(Matrix4f m) {
	// Print row by row
	LOG_DEBUG(
		"Matrix4f {\n"
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

void float4_print(float4 v) {
	LOG_INFO("Vector4f { %.2f, %.2f, %.2f, %.2f }", v.x, v.y, v.z, v.w);
}

void float3_print(float3 v) {
	LOG_INFO("Vector3f { %.2f, %.2f, %.2f }", v.x, v.y, v.z);
}
