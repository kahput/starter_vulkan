#ifndef CMATH_H_
#define CMATH_H_

#include <common.h>

#include <math.h>

#define PI 3.14159265358979323846264338327950288
#define PIf ((float)PI)
#define TAU (PI * 2)
#define TAUf (PIf * 2)
#define EPSILON 1e-5f
#define DEG2RAD (PIf / 180.f)
#define RAD2DEG (180.0f / PIf)

#define FLOAT_MAX 3.40282347e+38F

typedef struct {
	float elements[2 * 2];
} float2x2;
typedef struct {
	float elements[4 * 4];
} float4x4;
typedef float4 quat4;

// --- scalar ---
ENSURE_INLINE float deg_to_rad(float degree) { return degree * DEG2RAD; }
ENSURE_INLINE float rad_to_deg(float radians) { return radians * RAD2DEG; }

float randf_range(float min, float max);
uint32_t randu_range(uint32_t min, uint32_t max);
int32_t randi_range(int32_t min, int32_t max);
ENSURE_INLINE float clampf(float value, float min, float max) { return value < min ? min : (value > max ? max : value); }
ENSURE_INLINE float signf(float value) { return (value > 0.0f) - (value < 0.0f); }
ENSURE_INLINE float lerpf(float start, float end, float t) { return start + (end - start) * t; }
ENSURE_INLINE bool equalf(float a, float b) {
	if (a == b)
		return true;

	float tolerance = (float)EPSILON * fabsf(a);
	if (tolerance < (float)EPSILON)
		tolerance = (float)EPSILON;

	return fabsf(a - b) < tolerance;
}
ENSURE_INLINE bool zerof(float v) { return fabsf(v) < (float)EPSILON; }
ENSURE_INLINE float wrapf(float value, float min, float max) {
	float range = max - min;
	if (zerof(range))
		return min;

	float result = value - (range * floorf((value - min) / range));
	if (equalf(result, max))
		return min;

	return result;
}
ENSURE_INLINE float fractf(float v) { return v - floorf(v); }
ENSURE_INLINE float minf(float a, float b) { return a < b ? a : b; }
ENSURE_INLINE float maxf(float a, float b) { return a > b ? a : b; }

ENSURE_INLINE int32_t min1s(int32_t a, int32_t b) { return a < b ? a : b; }
ENSURE_INLINE int32_t max1s(int32_t a, int32_t b) { return a > b ? a : b; }

// --- float2 ---
ENSURE_INLINE void store2(float2 src, float dst[2]) { dst[0] = src.x, dst[1] = src.y; }
ENSURE_INLINE float2 load2(float v[2]) { return (float2){ v[0], v[1] }; }
#define spread2(v) v.x, v.y
#define cast2(v, T) ((T){ v.x, v.y })

ENSURE_INLINE bool equal2(float2 a, float2 b) { return equalf(a.x, b.x) && equalf(a.y, b.y); }
ENSURE_INLINE float2 negate2(float2 v) { return (float2){ -v.x, -v.y }; }

ENSURE_INLINE float2 add2(float2 a, float2 b) { return (float2){ a.x + b.x, a.y + b.y }; }
ENSURE_INLINE float2 sub2(float2 a, float2 b) { return (float2){ a.x - b.x, a.y - b.y }; }
ENSURE_INLINE float2 scale2(float2 v, float s) { return (float2){ v.x * s, v.y * s }; }

ENSURE_INLINE float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }

ENSURE_INLINE float len2_sq(float2 v) { return dot2(v, v); }
ENSURE_INLINE float len2(float2 v) { return sqrtf(dot2(v, v)); }

ENSURE_INLINE float2 norm2(float2 v) {
	float length = len2(v);
	if (length < EPSILON)
		return (float2){ 0 };

	return scale2(v, 1.0f / length);
}

ENSURE_INLINE float2 lerp2(float2 start, float2 end, float t) { return (float2){ lerpf(start.x, end.x, t), lerpf(start.y, end.y, t) }; }
ENSURE_INLINE float2 clamp2(float2 v, float min, float max) { return (float2){ clampf(v.x, min, max), clampf(v.y, min, max) }; }
ENSURE_INLINE float2 abs2(float2 v) { return (float2){ fabsf(v.x), fabsf(v.y) }; }

// --- float3 ---
ENSURE_INLINE void store3(float3 src, float dst[3]) { dst[0] = src.x, dst[1] = src.y, dst[2] = src.z; }
ENSURE_INLINE float3 load3(float v[3]) { return (float3){ v[0], v[1], v[2] }; }
#define spread3(v) v.x, v.y, v.z
#define cast3(v, T) ((T){ v.x, v.y, v.z })

ENSURE_INLINE bool equal3(float3 a, float3 b) { return equalf(a.x, b.x) && equalf(a.y, b.y) && equalf(a.z, b.z); }
ENSURE_INLINE float3 negate3(float3 v) { return (float3){ -v.x, -v.y, -v.z }; }

ENSURE_INLINE float3 add3(float3 a, float3 b) { return (float3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
ENSURE_INLINE float3 mul3(float3 a, float3 b) { return (float3){ a.x * b.x, a.y * b.y, a.z * b.z }; }
ENSURE_INLINE float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
ENSURE_INLINE float3 sub3(float3 a, float3 b) { return (float3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
ENSURE_INLINE float3 scale3(float3 v, float s) { return (float3){ v.x * s, v.y * s, v.z * s }; }

ENSURE_INLINE float3 cross3(float3 a, float3 b) { return (float3){ .x = a.y * b.z - b.y * a.z, .y = a.z * b.x - b.z * a.x, .z = a.x * b.y - b.x * a.y }; }
ENSURE_INLINE float len3_sq(float3 v) { return dot3(v, v); }
ENSURE_INLINE float len3(float3 v) { return sqrtf(dot3(v, v)); }

ENSURE_INLINE float3 norm3(float3 v) {
	float length = len3(v);
	if (length < EPSILON)
		return (float3){ 0 };

	return scale3(v, 1.0f / length);
}

ENSURE_INLINE float min3(float3 v) { return minf(v.x, minf(v.y, v.z)); }
ENSURE_INLINE float max3(float3 v) { return maxf(v.x, maxf(v.y, v.z)); }
ENSURE_INLINE float3 less3(float3 a, float3 b) { return (float3){ minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z) }; }
ENSURE_INLINE float3 more3(float3 a, float3 b) { return (float3){ maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z) }; }
ENSURE_INLINE float3 clamp3(float3 v, float min, float max) { return (float3){ clampf(v.x, min, max), clampf(v.y, min, max), clampf(v.z, min, max) }; }
ENSURE_INLINE float3 lerp3(float3 start, float3 end, float t) { return (float3){ lerpf(start.x, end.x, t), lerpf(start.y, end.y, t), lerpf(start.z, end.z, t) }; }

ENSURE_INLINE float angle3(float3 a, float3 b) { return acosf(clampf(dot3(norm3(a), norm3(b)), -1.0f, 1.0f)); }
float3 rotate3(float3 v, float angle, float3 axis);

// --- float4 & quaternions ---
ENSURE_INLINE void store4(float4 src, float dst[4]) { dst[0] = src.x, dst[1] = src.y, dst[2] = src.z, dst[3] = src.w; }
ENSURE_INLINE float4 load4(float v[4]) { return (float4){ .x = v[0], .y = v[1], .z = v[2], .w = v[3] }; }
#define spread4(v) v.x, v.y, v.z, v.w
#define cast4(v, T) ((T){ v.x, v.y, v.z, v.w })

ENSURE_INLINE bool equal4(float4 a, float4 b) { return equalf(a.x, b.x) && equalf(a.y, b.y) && equalf(a.z, b.z) && equalf(a.w, b.w); }

ENSURE_INLINE float4 scale4(float4 v, float s) { return (float4){ v.x * s, v.y * s, v.z * s, v.w * s }; }
ENSURE_INLINE float dot4(float4 a, float4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
ENSURE_INLINE float len4(float4 v) { return sqrtf(dot4(v, v)); }

ENSURE_INLINE quat4 quat4_identity(void) { return (quat4){ 0.0f, 0.0f, 0.0f, 1.0f }; }
float3 quat4_to_euler(quat4 quat);
quat4 quat4_from_axis_angle(float3 axis, float angle);
quat4 quat4_slerp(quat4 q, quat4 p, float t);

// --- float2x2 ---
float2x2 make2x2_from_rotation(float rad);
ENSURE_INLINE float2 mul2x2v(float2x2 m, float2 v) { return (float2){ m.elements[0] * v.x + m.elements[2] * v.y, m.elements[1] * v.x + m.elements[3] * v.y }; }

// --- float4x4 ---
bool equal4x4(float4x4 lhs, float4x4 rhs);
ENSURE_INLINE float4x4 identity4x4(void) { return (float4x4){ .elements[0] = 1.0f, .elements[5] = 1.0f, .elements[10] = 1.0f, .elements[15] = 1.0f }; }
float4x4 mul4x4(float4x4 lhs, float4x4 rhs);
float4 mul4x4v(float4x4 m, float4 v);

float4x4 translate4x4(float4x4 matrix, float3 translation);
float4x4 rotate4x4(float4x4 matrix, float3 axis, float radians_angle);
float4x4 scale4x4(float4x4 matrix, float3 scale);

float4x4 make4x4_translation(float3 translation);
float4x4 make4x4_rotation(float3 axis, float angle);
float4x4 make4x4_scale(float3 scale);
float4x4 make4x4_quat(quat4 q);

float4x4 transpose4x4(float4x4 m);

float4x4 compose4x4_euler(float3 position, float3 radians_angle, float3 scale);
float4x4 compose4x4_quat(float3 position, quat4 rotation, float3 scale);

float4x4 perspective(float radians_fovy, float aspect, float near_z, float far_z);
float4x4 orthographic(float left, float right, float top, float bottom, float near, float far);

float4x4 lookat(float3 eye, float3 center, float3 up);

#endif /* CMATH_H_ */
