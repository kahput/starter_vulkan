#ifndef CMATH_H_
#define CMATH_H_

#include <common.h>

#include <math.h>

#define PI 3.14159265358979323846264338327950288
#define PIf ((float)PI)
#define TAU (PI * 2)
#define TAUf (PIf * 2)
#define EPSILON 1e-5f

#define FLOAT_MAX 3.40282347e+38F

/* #define forward3f (float3){ 0.0f, 0.0f, 1.0f } */
/* #define backwardf (float3){ 0.0f, 0.0f, -1.0f } */

#define zero2 (float2){ 0.0f, 0.0f }
#define one2 (float2){ 1.0f, 1.0f }

#define zero3 (float3){ 0.0f, 0.0f, 0.0f }
#define one3 (float3){ 1.0f, 1.0f, 1.0f }

#define zero4 (float4){ 0.0f, 0.0f, 0.0f, 0.0f }
#define one4 (float4){ 1.0f, 1.0f, 1.0f, 1.0f }

typedef struct {
	float elements[2 * 2];
} float2x2;
typedef struct {
	float elements[4 * 4];
} float4x4;
typedef float4 quat4;

// --- scalar ---
extern const float DEG2RAD;
extern const float RAD2DEG;
static inline float deg_to_rad(float degree) { return degree * DEG2RAD; }
static inline float rad_to_deg(float radians) { return radians * RAD2DEG; }

float randf_range(float min, float max);
uint32_t randu_range(uint32_t min, uint32_t max);
int32_t randi_range(int32_t min, int32_t max);
static inline float clampf(float value, float min, float max) { return value < min ? min : (value > max ? max : value); }
static inline float signf(float value) { return (value > 0.0f) - (value < 0.0f); }
static inline float lerpf(float start, float end, float t) { return start + (end - start) * t; }
static inline bool equalf(float a, float b) {
	if (a == b)
		return true;

	float tolerance = (float)EPSILON * fabsf(a);
	if (tolerance < (float)EPSILON)
		tolerance = (float)EPSILON;

	return fabsf(a - b) < tolerance;
}
static inline bool zerof(float v) { return fabsf(v) < (float)EPSILON; }
static inline float wrapf(float value, float min, float max) {
	float range = max - min;
	if (zerof(range))
		return min;

	float result = value - (range * floorf((value - min) / range));
	if (equalf(result, max))
		return min;

	return result;
}
static inline float fractf(float v) { return v - floorf(v); }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }

// --- float2 ---
static inline float2 cast2df(double2 d) { return (float2){ (float)d.x, (float)d.y }; }
static inline float2 cast2uf(uint2 u) { return (float2){ (float)u.x, (float)u.y }; }
static inline float2 float2_from_float3(float3 v) { return (float2){ v.x, v.y }; }

#define spread2(v) v.x, v.y
#define cast2(v, T) ((T){ v.x, v.y })

static inline bool equal2(float2 a, float2 b) { return equalf(a.x, b.x) && equalf(a.y, b.y); }
static inline float2 negate2(float2 v) { return (float2){ -v.x, -v.y }; }

static inline float2 add2(float2 a, float2 b) { return (float2){ a.x + b.x, a.y + b.y }; }
static inline float2 sub2(float2 a, float2 b) { return (float2){ a.x - b.x, a.y - b.y }; }
static inline float2 scale2(float2 v, float s) { return (float2){ v.x * s, v.y * s }; }

static inline float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }

static inline float lensq2(float2 v) { return dot2(v, v); }
float len(float2 v);

static inline float2 norm2(float2 v) {
	float length = len(v);
	if (length < EPSILON)
		return (float2){ 0 };

	return scale2(v, 1.0f / len(v));
}

static inline float2 lerp2(float2 start, float2 end, float t) { return (float2){ lerpf(start.x, end.x, t), lerpf(start.y, end.y, t) }; }
static inline float2 clamp2(float2 v, float min, float max) { return (float2){ clampf(v.x, min, max), clampf(v.y, min, max) }; }
static inline float2 abs2(float2 v) { return (float2){ fabsf(v.x), fabsf(v.y) }; }

// --- float3 ---
static inline float3 wrap3(float v[3]) { return (float3){ v[0], v[1], v[2] }; }
#define spread3(v) v.x, v.y, v.z
#define cast3(v, T) ((T){ v.x, v.y, v.z })

static inline bool equal3f(float3 a, float3 b) { return equalf(a.x, b.x) && equalf(a.y, b.y) && equalf(a.z, b.z); }
static inline float3 negate3f(float3 v) { return (float3){ -v.x, -v.y, -v.z }; }

static inline float3 add3(float3 a, float3 b) { return (float3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline float3 mul3(float3 a, float3 b) { return (float3){ a.x * b.x, a.y * b.y, a.z * b.z }; }
static inline float3 sub3(float3 a, float3 b) { return (float3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline float3 scale3(float3 v, float s) { return (float3){ v.x * s, v.y * s, v.z * s }; }

static inline float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float3 cross3(float3 a, float3 b) { return (float3){ .x = a.y * b.z - b.y * a.z, .y = a.z * b.x - b.z * a.x, .z = a.x * b.y - b.x * a.y }; }
static inline float lensq3(float3 v) { return dot3(v, v); }
float len3(float3 v);

static inline float3 norm3(float3 v) {
	float length = len3(v);
	if (length < EPSILON)
		return (float3){ 0 };

	return scale3(v, 1.0f / len3(v));
}

static inline float min3(float3 v) { return minf(v.x, minf(v.y, v.z)); }
static inline float max3(float3 v) { return maxf(v.x, maxf(v.y, v.z)); }
static inline float3 less3(float3 a, float3 b) { return (float3){ minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z) }; }
static inline float3 more3(float3 a, float3 b) { return (float3){ maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z) }; }
static inline float3 clamp3(float3 v, float min, float max) { return (float3){ clampf(v.x, min, max), clampf(v.y, min, max), clampf(v.z, min, max) }; }
static inline float3 lerp3(float3 start, float3 end, float t) { return (float3){ lerpf(start.x, end.x, t), lerpf(start.y, end.y, t), lerpf(start.z, end.z, t) }; }

float angle3(float3 a, float3 b);
float3 rotate3(float3 v, float angle, float3 axis);

// --- float4 & quaternions ---
static inline float4 wrap4(float v[4]) { return (float4){ .x = v[0], .y = v[1], .z = v[2], .w = v[3] }; }
#define spread4(v) v.x, v.y, v.z, v.w
#define cast4(v, T) ((T){ v.x, v.y, v.z, v.w })

static inline bool equal4(float4 a, float4 b) { return equalf(a.x, b.x) && equalf(a.y, b.y) && equalf(a.z, b.z) && equalf(a.w, b.w); }

static inline float4 scale4(float4 v, float s) { return (float4){ v.x * s, v.y * s, v.z * s, v.w * s }; }
static inline float dot4(float4 a, float4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
float len4(float4 v);

static inline quat4 quat4_identity(void) { return (quat4){ 0.0f, 0.0f, 0.0f, 1.0f }; }

float3 quat4_to_euler(quat4 quat);
quat4 quat4_from_axis_angle(float3 axis, float angle);
quat4 quat4_slerp(quat4 q, quat4 p, float t);

// --- float2x2 ---
float2x2 rot2x2(float rad);
static inline float2 mul2x2v(float2x2 m, float2 v) { return (float2){ m.elements[0] * v.x + m.elements[2] * v.y, m.elements[1] * v.x + m.elements[3] * v.y }; }

// --- float4x4 ---
bool float4x4_equal(float4x4 lhs, float4x4 rhs);
float4x4 float4x4_identity(void);
float4x4 mul4x4(float4x4 lhs, float4x4 rhs);
float4 mul4x4v(float4x4 m, float4 v);

float4x4 float4x4_translate(float4x4 matrix, float3 translation);
float4x4 float4x4_rotate(float4x4 matrix, float angle_radians, float3 axis);
float4x4 float4x4_scale(float4x4 matrix, float3 scale);

float4x4 float4x4_translation(float3 translation);
float4x4 float4x4_rotation(float3 axis, float angle);
float4x4 float4x4_scaling(float3 scale);
float4x4 float4x4_from_quat(quat4 q);

float4x4 transpose4x4(float4x4 m);

float4x4 float4x4_compose(float3 position, float3 rotation_rad, float3 scale);
float4x4 float4x4_compose_quat(float3 position, float4 rotation, float3 scale);
float3 float4x4_transform(float4x4 m, float4 v);

float4x4 float4x4_perspective(float fovy_radians, float aspect,
	float near_z, float far_z);
float4x4 float4x4_orthographic(float left, float right, float top,
	float bottom, float near, float far);

float4x4 float4x4_lookat(float3 eye, float3 center, float3 up);

#endif /* CMATH_H_ */
