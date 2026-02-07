#ifndef CMATH_H_
#define CMATH_H_

#include <common.h>

#define C_PI 3.14159265358979323846264338327950288
#define C_PIf ((float)C_PI)

#define EPSILON 1e-6f

#define DEG2RAD(x) ((x) * C_PIf / 180.0f)
#define RAD2DEG(x) ((x) * (180 / C_PIf))

typedef struct {
	float elements[4 * 4];
} Matrix4f;
STATIC_ASSERT(sizeof(Matrix4f) == (4 * 4) * sizeof(float));

static inline float *float4_elements(float4 *v) { return (float *)v; }
static inline float *float3_elements(float3 *v) { return (float *)v; }
static inline float *float2_elements(float2 *v) { return (float *)v; }

ENGINE_API float3 float3_add(float3 a, float3 b);
ENGINE_API float3 float3_subtract(float3 a, float3 b);
ENGINE_API float3 float3_scale(float3 v, float s);

ENGINE_API float3 float3_negate(float3 v);

ENGINE_API float float3_dot(float3 a, float3 b);
ENGINE_API float3 float3_cross(float3 a, float3 b);

ENGINE_API float float3_length(float3 v);
ENGINE_API float3 float3_normalize(float3 v);
ENGINE_API float3 float3_normalize_safe(float3 v, float epsilon);

ENGINE_API float float3_angle(float3 a, float3 b);
ENGINE_API float3 float3_rotate(float3 v, float angle, float3 axis);

// float vec4f_dot_product(float4 v);

ENGINE_API Matrix4f mat4f_identity(void);
ENGINE_API Matrix4f mat4f_multiply(Matrix4f lhs, Matrix4f rhs);

ENGINE_API Matrix4f mat4f_translate(Matrix4f matrix, float3 translation);
ENGINE_API Matrix4f mat4f_rotate(Matrix4f matrix, float angle, float3 axis);
ENGINE_API Matrix4f mat4f_scale(Matrix4f matrix, float3 scale);

ENGINE_API Matrix4f mat4f_translated(float3 translation);
ENGINE_API Matrix4f mat4f_rotated(float angle, float3 axis);
ENGINE_API Matrix4f mat4f_scaled(float3 scale);

ENGINE_API Matrix4f mat4f_perspective(float fovy_radians, float aspect, float near_z, float far_z);
ENGINE_API Matrix4f mat4f_orthographic(float left, float right, float top, float bottom, float near, float far);

ENGINE_API Matrix4f mat4f_lookat(float3 eye, float3 center, float3 up);

ENGINE_API void mat4f_debug_print(Matrix4f m);
ENGINE_API void vec4f_debug_print(float4 v);
ENGINE_API void float3_debug_print(float3 v);

#endif /* CMATH_H_ */
