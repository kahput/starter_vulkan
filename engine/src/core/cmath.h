#ifndef CMATH_H_
#define CMATH_H_

#include <common.h>

#define C_PI 3.14159265358979323846264338327950288
#define C_PIf ((float)C_PI)
#define EPSILON 1e-6f

typedef struct {
	float elements[4 * 4];
} float4x4;
typedef float32x2 float2;
typedef float32x3 float3;
typedef float32x3 ray3;
typedef float32x4 float4;

#define FLOAT_MAX 3.40282347e+38F
#define FLOAT_MIN -FLOAT_MAX

#define FLOAT3_X (float3){ 1.0f, 0.0f, 0.0f }
#define FLOAT3_Y (float3){ 0.0f, 1.0f, 0.0f }
#define FLOAT3_Z (float3){ 0.0f, 0.0f, 1.0f }
#define FLOAT3_ONE (float3){ 1.0f, 1.0f, 1.0f }

static inline float deg2radf(float degree) { return degree * (C_PIf / 180.f); }
static inline float rad2degf(float radians) {
	return radians * (180.0f / C_PIf);
}

static inline float clampf(float value, float min, float max) {
	return value < min ? min : (value > max ? max : value);
}
static inline float signf(float value) { return (value > 0.0f) - (value < 0.0f); }
static inline float lerpf(float start, float end, float t) { return start + (end - start) * t; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }

static inline float2 float2_make(float x, float y) { return (float2){ x, y }; }
static inline float2 float2_from_double2(double2 d) { return (float2){ (float)d.x, (float)d.y }; }
static inline float2 float2_from_uint2(uint2 u) { return (float2){ (float)u.x, (float)u.y }; }

ENGINE_API float2 float2_negate(float2 v);

ENGINE_API float float2_length(float2 v);
ENGINE_API float2 float2_normalize(float2 v);

ENGINE_API float2 float2_add(float2 a, float2 b);
ENGINE_API float2 float2_subtract(float2 a, float2 b);
ENGINE_API float2 float2_divide(float2 a, float2 b);
ENGINE_API float2 float2_scale(float2 v, float s);

ENGINE_API float2 float2_lerp(float2 start, float2 end, float t);
ENGINE_API float2 float2_clamp(float2 v, float2 min, float2 max);

static inline float3 float3_fill(float value) { return (float3){ value, value, value }; }
static inline float3 float3_wrap(float v[3]) {
	return (float3){ .x = v[0], .y = v[1], .z = v[2] };
}

ENGINE_API float3 float3_negate(float3 v);
ENGINE_API float float2_dot(float2 a, float2 b);

ENGINE_API float3 float3_add(float3 a, float3 b);
ENGINE_API float3 float3_subtract(float3 a, float3 b);
ENGINE_API float3 float3_scale(float3 v, float s);

ENGINE_API float float3_dot(float3 a, float3 b);
ENGINE_API float3 float3_cross(float3 a, float3 b);

ENGINE_API float float3_length(float3 v);
ENGINE_API float3 float3_normalize(float3 v);
ENGINE_API float3 float3_normalize_safe(float3 v, float epsilon);

ENGINE_API float3 float3_min(float3 a, float3 b);
ENGINE_API float3 float3_max(float3 a, float3 b);
ENGINE_API float3 float3_clamp(float3 v, float3 min, float3 max);
ENGINE_API float3 float3_lerp(float3 start, float3 end, float t);

ENGINE_API float float3_angle(float3 a, float3 b);
ENGINE_API float3 float3_rotate(float3 v, float angle, float3 axis);

// float float4_dot_product(Vector4f v);

ENGINE_API float4x4 float4x4_identity(void);
ENGINE_API float4x4 float4x4_multiply(float4x4 lhs, float4x4 rhs);

ENGINE_API float4x4 float4x4_translate(float4x4 matrix, float3 translation);
ENGINE_API float4x4 float4x4_rotate(float4x4 matrix, float angle_radians, float3 axis);
ENGINE_API float4x4 float4x4_scale(float4x4 matrix, float3 scale);

ENGINE_API float4x4 float4x4_translation(float3 translation);
ENGINE_API float4x4 float4x4_rotation(float angle, float3 axis);
ENGINE_API float4x4 float4x4_scaling(float3 scale);

ENGINE_API float4x4 float4x4_perspective(float fovy_radians, float aspect,
	float near_z, float far_z);
ENGINE_API float4x4 float4x4_orthographic(float left, float right, float top,
	float bottom, float near, float far);

ENGINE_API float4x4 float4x4_lookat(float3 eye, float3 center, float3 up);

ENGINE_API void float4x4_print(float4x4 m);
ENGINE_API void float4_print(float4 v);
ENGINE_API void float3_print(float3 v);

#endif /* CMATH_H_ */
