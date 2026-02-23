#include "cmath.h"
#include "core/logger.h"

#include <math.h>

Vector3f vector3f_add(Vector3f a, Vector3f b) {
	Vector3f result = { a.x + b.x, a.y + b.y, a.z + b.z };

	return result;
}
Vector3f vector3f_subtract(Vector3f a, Vector3f b) {
	Vector3f result = { a.x - b.x, a.y - b.y, a.z - b.z };

	return result;
}
Vector3f vector3f_scale(Vector3f v, float s) {
	Vector3f result = { v.x * s, v.y * s, v.z * s };

	return result;
}

Vector3f vector3f_negate(Vector3f v) { return (Vector3f){ -v.x, -v.y, -v.z }; }

float vector3f_dot(Vector3f a, Vector3f b) {
	float result = a.x * b.x + a.y * b.y + a.z * b.z;

	return result;
}

Vector3f vector3f_cross(Vector3f a, Vector3f b) {
	Vector3f result = {
		.x = a.y * b.z - b.y * a.z,
		.y = a.z * b.x - b.z * a.x,
		.z = a.x * b.y - b.x * a.y,
	};

	return result;
}

float vector3f_length(Vector3f v) {
	float result = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);

	return result;
}

Vector3f vector3f_normalize(Vector3f v) {
	Vector3f result = vector3f_scale(v, 1 / vector3f_length(v));

	return result;
}
Vector3f vector3f_normalize_safe(Vector3f v, float epsilon) {
	float length = vector3f_length(v);
	if (length < epsilon) {
		return (Vector3f){ 0 }; // Return zero vectortor if too small
	}
	return vector3f_scale(v, 1.0f / length);
}

float vector3f_angle(Vector3f a, Vector3f b) {
	float dot = vector3f_dot(vector3f_normalize_safe(a, EPSILON), vector3f_normalize_safe(b, EPSILON));

	if (dot > 1.0f)
		dot = 1.0f;
	if (dot < -1.0f)
		dot = -1.0f;

	return acosf(dot);
}

/* Right Hand, Rodrigues' rotation formula:
	v = v*cos(t) + (kxv)sin(t) + k*(k.v)(1 - cos(t))
*/
Vector3f vector3f_rotate(Vector3f v, float angle, Vector3f axis) {
	float c = cosf(angle);
	float s = sinf(angle);
	Vector3f k = vector3f_normalize_safe(axis, EPSILON);

	return vector3f_add(
		vector3f_scale(v, c),
		vector3f_add(
			vector3f_scale(vector3f_cross(k, v), s),
			vector3f_scale(k, vector3f_dot(k, v) * (1.0f - c))));
}

Matrix4f matrix4f_identity(void) {
	Matrix4f result = { { 1.0f, 0.0f, 0.0f, 0.0f,
	  0.0f, 1.0f, 0.0f, 0.0f,
	  0.0f, 0.0f, 1.0f, 0.0f,
	  0.0f, 0.0f, 0.0f, 1.0f } };

	return result;
}

Matrix4f matrix4f_multiply(Matrix4f lhs, Matrix4f rhs) {
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

Matrix4f matrix4f_translate(Matrix4f m, Vector3f t) {
	Matrix4f result = m;

	result.elements[12] = m.elements[0] * t.x + m.elements[4] * t.y + m.elements[8] * t.z + m.elements[12];
	result.elements[13] = m.elements[1] * t.x + m.elements[5] * t.y + m.elements[9] * t.z + m.elements[13];
	result.elements[14] = m.elements[2] * t.x + m.elements[6] * t.y + m.elements[10] * t.z + m.elements[14];
	result.elements[15] = m.elements[3] * t.x + m.elements[7] * t.y + m.elements[11] * t.z + m.elements[15];

	return result;
}

Matrix4f matrix4f_scale(Matrix4f m, Vector3f s) {
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

Matrix4f matrix4f_rotate(Matrix4f m, float angle, Vector3f axis) {
	// Post-multiply by rotation: result = m * R
	// Means: each of the first 3 columns of m gets mixed by R.
	Matrix4f r = matrix4f_rotated(angle, axis);
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

Matrix4f matrix4f_translated(Vector3f v) {
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

Matrix4f matrix4f_rotated(float angle, Vector3f axis) {
	float c = cosf(angle);
	float s = sinf(angle);
	float t = 1.0f - c;

	Vector3f normalized_axis = vector3f_normalize_safe(axis, EPSILON);
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

Matrix4f matrix4f_scaled(Vector3f scale) {
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

Matrix4f matrix4f_perspective(float fovy_radians, float aspect, float near_z, float far_z) {
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

Matrix4f matrix4f_orthographic(float left, float right, float bottom, float top, float near_z, float far_z) {
	Matrix4f result = matrix4f_identity();

	// FIX: Using Vulkan Zero-to-One Depth (ZO)
	//
	// Top-Left is (-1, -1), Bottom-Right is (1, 1), Z is 0 to 1

	result.elements[0] = 2.0f / (right - left);
	result.elements[5] = 2.0f / (bottom - top); // Note: bottom - top to flip Y for Vulkan if needed, or standard top-bottom
	result.elements[10] = 1.0f / (near_z - far_z); // Scale for [0, 1] range

	result.elements[12] = -(right + left) / (right - left);
	result.elements[13] = -(bottom + top) / (bottom - top);
	result.elements[14] = near_z / (near_z - far_z); // Translation for [0, 1] range

	return result;
}

Matrix4f matrix4f_lookat(Vector3f eye, Vector3f center, Vector3f up) {
	Matrix4f result = matrix4f_identity();

	// 1. Calculate Basis Vectors
	// Forward (f): Points from eye to center (standard OpenGL/Vulkan convention is -Z forward,
	// so we actually want the vectortor pointing OUT of the screen for the Z-axis basis).
	// Let's stick to standard Right-Handed Rule derivation:
	// f = normalized(center - eye) -> Direction usually labeled "Forward"
	// z_axis = -f                 -> The actual Z column of the matrix
	Vector3f f = vector3f_normalize_safe(vector3f_subtract(center, eye), EPSILON);

	// Right (s)
	Vector3f s = vector3f_normalize_safe(vector3f_cross(f, up), EPSILON);

	// Up (u)
	Vector3f u = vector3f_cross(s, f);

	// 2. Fill Matrix
	// The View Matrix Rotation is the Transpose of the Camera orientation.
	// So the Basis Vectors (s, u, -f) become the ROWS of the matrix.

	// Row 0: Right Vector (s)
	result.elements[0] = s.x; // Col 0
	result.elements[4] = s.y; // Col 1
	result.elements[8] = s.z; // Col 2
	result.elements[12] = -vector3f_dot(s, eye); // Translation X

	// Row 1: Up Vector (u)
	result.elements[1] = u.x; // Col 0
	result.elements[5] = u.y; // Col 1
	result.elements[9] = u.z; // Col 2
	result.elements[13] = -vector3f_dot(u, eye); // Translation Y

	// Row 2: Back Vector (-f)
	// (Note: we use -f because the camera looks down -Z)
	result.elements[2] = -f.x; // Col 0
	result.elements[6] = -f.y; // Col 1
	result.elements[10] = -f.z; // Col 2
	result.elements[14] = vector3f_dot(f, eye); // Translation Z (-dot(-f, eye))

	// Row 3: Identity
	result.elements[3] = 0.0f;
	result.elements[7] = 0.0f;
	result.elements[11] = 0.0f;
	result.elements[15] = 1.0f;

	return result;
}

void matrix4f_print(Matrix4f m) {
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

void vector4f_print(Vector4f v) {
	LOG_INFO("Vector4f { %.2f, %.2f, %.2f, %.2f }", v.x, v.y, v.z, v.w);
}

void vector3f_print(Vector3f v) {
	LOG_INFO("Vector3f { %.2f, %.2f, %.2f }", v.x, v.y, v.z);
}
