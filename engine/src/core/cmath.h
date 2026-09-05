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
INLINE float deg_to_rad(float degree) { return degree * DEG2RAD; }
INLINE float rad_to_deg(float radians) { return radians * RAD2DEG; }

float randf_range(float min, float max);
uint32_t randu_range(uint32_t min, uint32_t max);
int32_t randi_range(int32_t min, int32_t max);
INLINE float clampf(float value, float min, float max) { return value < min ? min : (value > max ? max : value); }
INLINE float signf(float value) { return (value > 0.0f) - (value < 0.0f); }
INLINE float sqf(float v) { return v* v; }
INLINE float lerpf(float start, float end, float t) { return start + (end - start) * t; }
INLINE bool eqf(float a, float b) {
	if (a == b)
		return true;

	float tolerance = (float)EPSILON * fabsf(a);
	if (tolerance < (float)EPSILON)
		tolerance = (float)EPSILON;

	return fabsf(a - b) < tolerance;
}
INLINE bool zerof(float v) { return fabsf(v) < (float)EPSILON; }
INLINE float wrapf(float value, float min, float max) {
	float range = max - min;
	if (zerof(range))
		return min;

	float result = value - (range * floorf((value - min) / range));
	if (eqf(result, max))
		return min;

	return result;
}
INLINE float fractf(float v) { return v - floorf(v); }
INLINE float minf(float a, float b) { return a < b ? a : b; }
INLINE float maxf(float a, float b) { return a > b ? a : b; }

INLINE int32_t min1s(int32_t a, int32_t b) { return a < b ? a : b; }
INLINE int32_t max1s(int32_t a, int32_t b) { return a > b ? a : b; }

// --- float2 ---
INLINE void store2(float2 src, float dst[2]) { dst[0] = src.x, dst[1] = src.y; }
INLINE float2 load2(const float v[2]) { return (float2){ v[0], v[1] }; }
#define spread2(v) v.x, v.y
#define as2(v, T) ((T){ v.x, v.y })

INLINE bool eq2(float2 a, float2 b) { return eqf(a.x, b.x) && eqf(a.y, b.y); }
INLINE float2 negate2(float2 v) { return (float2){ -v.x, -v.y }; }

INLINE float2 add2(float2 a, float2 b) { return (float2){ a.x + b.x, a.y + b.y }; }
INLINE float2 sub2(float2 a, float2 b) { return (float2){ a.x - b.x, a.y - b.y }; }
INLINE float2 mul2(float2 a, float2 b) { return (float2){ a.x * b.x, a.y * b.y }; }
INLINE float2 scale2(float2 v, float s) { return (float2){ v.x * s, v.y * s }; }

INLINE float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }

INLINE float lensq2(float2 v) { return dot2(v, v); }
INLINE float len2(float2 v) { return sqrtf(dot2(v, v)); }
INLINE float dist2(float2 a, float2 b) { return len2(sub2(b, a)); }
INLINE float distsq2(float2 a, float2 b) { return lensq2(sub2(b, a)); }

INLINE float2 norm2(float2 v) {
	float length = len2(v);
	if (length < EPSILON)
		return (float2){ 0 };

	return scale2(v, 1.0f / length);
}

INLINE float2 lerp2(float2 start, float2 end, float t) { return (float2){ lerpf(start.x, end.x, t), lerpf(start.y, end.y, t) }; }
INLINE float2 mid2(float2 a, float2 b) { return scale2(add2(a, b), 0.5f); }
INLINE float2 clamp2(float2 v, float min, float max) { return (float2){ clampf(v.x, min, max), clampf(v.y, min, max) }; }
INLINE float2 abs2(float2 v) { return (float2){ fabsf(v.x), fabsf(v.y) }; }
INLINE float2 rotate2(float2 v, float rad) {
	float c = cosf(rad);
	float s = sinf(rad);
	return (float2){
		v.x * c - v.y * s,
		v.x * s + v.y * c
	};
}

// --- float3 ---
INLINE void store3(float3 src, float dst[3]) { dst[0] = src.x, dst[1] = src.y, dst[2] = src.z; }
INLINE float3 load3(const float v[3]) { return (float3){ v[0], v[1], v[2] }; }
#define spread3(v) (v).x, (v).y, (v).z
#define as3(v, T) ((T){ (v).x, (v).y, (v).z })

INLINE bool eq3(float3 a, float3 b) { return eqf(a.x, b.x) && eqf(a.y, b.y) && eqf(a.z, b.z); }
INLINE float3 neg3(float3 v) { return (float3){ -v.x, -v.y, -v.z }; }

INLINE float3 add3(float3 a, float3 b) { return (float3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
INLINE float3 sub3(float3 a, float3 b) { return (float3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
INLINE float3 mul3(float3 a, float3 b) { return (float3){ a.x * b.x, a.y * b.y, a.z * b.z }; }
INLINE float3 scale3(float3 v, float s) { return (float3){ v.x * s, v.y * s, v.z * s }; }
INLINE float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
INLINE float3 madd3(float3 a, float3 b, float s) { return (float3){ fmaf(b.x, s, a.x), fmaf(b.y, s, a.y), fmaf(b.z, s, a.z) }; }

INLINE float3 cross3(float3 a, float3 b) { return (float3){ .x = a.y * b.z - b.y * a.z, .y = a.z * b.x - b.z * a.x, .z = a.x * b.y - b.x * a.y }; }
INLINE float lensq3(float3 v) { return dot3(v, v); }
INLINE float len3(float3 v) { return sqrtf(dot3(v, v)); }
INLINE float dist3(float3 a, float3 b) { return len3(sub3(b, a)); }
INLINE float distsq3(float3 a, float3 b) { return lensq3(sub3(b, a)); }

INLINE float3 norm3(float3 v) {
	float length = len3(v);
	if (length < EPSILON)
		return (float3){ 0 };

	return scale3(v, 1.0f / length);
}

INLINE float hmin3(float3 v) { return minf(v.x, minf(v.y, v.z)); }
INLINE float hmax3(float3 v) { return maxf(v.x, maxf(v.y, v.z)); }

INLINE float3 abs3(float3 v) { return (float3){ fabsf(v.x), fabsf(v.y), fabsf(v.z) }; }
INLINE float3 min3(float3 a, float3 b) { return (float3){ minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z) }; }
INLINE float3 max3(float3 a, float3 b) { return (float3){ maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z) }; }
INLINE float3 clamp3(float3 v, float min, float max) { return (float3){ clampf(v.x, min, max), clampf(v.y, min, max), clampf(v.z, min, max) }; }
INLINE float3 approach3(float3 start, float3 end, float step) {
	float3 vd = sub3(end, start);
	float len = len3(vd);

	if (len <= step || len < EPSILON)
		return end;

	return add3(start, scale3(vd, step / len));
}
INLINE float3 lerp3(float3 start, float3 end, float t) { return madd3(start, sub3(end, start), t); }
INLINE float3 orthobasis3(float3 normal, float3 *right, float3 *up) {
	normal = norm3(normal);
	*right = norm3(cross3(normal, fabsf(dot3(unit3(UP), normal)) >= 0.99f ? unit3(BACKWARD) : unit3(UP)));
	*up = cross3(normal, *right);
	return normal;
}

INLINE float angle3(float3 a, float3 b) { return acosf(clampf(dot3(norm3(a), norm3(b)), -1.0f, 1.0f)); }
float3 rotate3(float3 v, float angle, float3 axis);

// --- float4 & quaternions ---
INLINE void store4(float4 src, float dst[4]) { dst[0] = src.x, dst[1] = src.y, dst[2] = src.z, dst[3] = src.w; }
INLINE float4 load4(const float v[4]) { return (float4){ .x = v[0], .y = v[1], .z = v[2], .w = v[3] }; }
#define spread4(v) v.x, v.y, v.z, v.w
#define as4(v, T) ((T){ v.x, v.y, v.z, v.w })

INLINE bool eq4(float4 a, float4 b) { return eqf(a.x, b.x) && eqf(a.y, b.y) && eqf(a.z, b.z) && eqf(a.w, b.w); }

INLINE float4 scale4(float4 v, float s) { return (float4){ v.x * s, v.y * s, v.z * s, v.w * s }; }
INLINE float dot4(float4 a, float4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
INLINE float len4(float4 v) { return sqrtf(dot4(v, v)); }

INLINE quat4 quat4_identity(void) { return (quat4){ 0.0f, 0.0f, 0.0f, 1.0f }; }
float3 quat4_to_euler(quat4 quat);
quat4 quat4_from_axis_angle(float3 axis, float angle);
quat4 quat4_slerp(quat4 q, quat4 p, float t);

// --- float2x2 ---
float2x2 make2x2_from_rotation(float rad);
INLINE float2 mul2x2v(float2x2 m, float2 v) { return (float2){ m.elements[0] * v.x + m.elements[2] * v.y, m.elements[1] * v.x + m.elements[3] * v.y }; }

// --- float4x4 ---
bool eq4x4(float4x4 lhs, float4x4 rhs);
INLINE float4x4 identity4x4(void) { return (float4x4){ .elements[0] = 1.0f, .elements[5] = 1.0f, .elements[10] = 1.0f, .elements[15] = 1.0f }; }
float4x4 mul4x4(float4x4 lhs, float4x4 rhs);
float4 mul4x4v(float4x4 m, float4 v);

float4x4 translation4x4(float3 translation);
float4x4 basis4x4(float3 right, float3 up, float3 forward);
float4x4 axis_angle4x4(float3 axis, float angle);
float4x4 quat4x4(quat4 q);
float4x4 scale4x4(float3 scale);

float4x4 transpose4x4(float4x4 m);

float4x4 compose4x4_euler(float3 position, float3 radians_angle, float3 scale);
float4x4 compose4x4_quat(float3 position, quat4 rotation, float3 scale);

float4x4 perspective(float radians_fovy, float aspect, float near_z, float far_z);
float4x4 orthographic(float left, float right, float top, float bottom, float near, float far);

float4x4 lookat(float3 eye, float3 center, float3 up);

#endif /* CMATH_H_ */
