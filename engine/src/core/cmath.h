#ifndef CMATH_H_
#define CMATH_H_

#include <common.h>

#define C_PI 3.14159265358979323846264338327950288
#define C_PIf ((float)C_PI)

#define EPSILON 1e-6f

#define DEG2RAD(x) ((x) * C_PIf / 180.0f)
#define RAD2DEG(x) ((x) * (180 / C_PIf))

typedef struct {
	float x, y;
} Vector2f;
STATIC_ASSERT(sizeof(Vector2f) == 2 * sizeof(float));

typedef struct {
    uint32_t x, y, z;
} Vector3u;
STATIC_ASSERT(sizeof(Vector3u) == 3 * sizeof(uint32_t));

typedef struct {
	float x, y, z;
} Vector3f;
STATIC_ASSERT(sizeof(Vector3f) == 3 * sizeof(float));

typedef struct {
	float x, y, z, w;
} Vector4f;
STATIC_ASSERT(sizeof(Vector4f) == 4 * sizeof(float));

typedef struct {
	float elements[4 * 4];
} Matrix4f;
STATIC_ASSERT(sizeof(Matrix4f) == (4 * 4) * sizeof(float));

static inline float *vec4f_elements(Vector4f *v) { return (float *)v; }
static inline float *vec3f_elements(Vector3f *v) { return (float *)v; }
static inline float *vec2f_elements(Vector2f *v) { return (float *)v; }

ENGINE_API Vector3f vec3f_add(Vector3f a, Vector3f b);
ENGINE_API Vector3f vec3f_subtract(Vector3f a, Vector3f b);
ENGINE_API Vector3f vec3f_scale(Vector3f v, float s);

ENGINE_API Vector3f vec3f_negate(Vector3f v);

ENGINE_API float vec3f_dot(Vector3f a, Vector3f b);
ENGINE_API Vector3f vec3f_cross(Vector3f a, Vector3f b);

ENGINE_API float vec3f_length(Vector3f v);
ENGINE_API Vector3f vec3f_normalize(Vector3f v);
ENGINE_API Vector3f vec3f_normalize_safe(Vector3f v, float epsilon);

ENGINE_API float vec3f_angle(Vector3f a, Vector3f b);
ENGINE_API Vector3f vec3f_rotate(Vector3f v, float angle, Vector3f axis);

// float vec4f_dot_product(Vector4f v);

ENGINE_API Matrix4f mat4f_identity(void);
ENGINE_API Matrix4f mat4f_multiply(Matrix4f lhs, Matrix4f rhs);

ENGINE_API Matrix4f mat4f_translate(Matrix4f matrix, Vector3f translation);
ENGINE_API Matrix4f mat4f_rotate(Matrix4f matrix, float angle,  Vector3f axis);
ENGINE_API Matrix4f mat4f_scale(Matrix4f matrix, Vector3f scale);

ENGINE_API Matrix4f mat4f_translated(Vector3f translation);
ENGINE_API Matrix4f mat4f_rotated(float angle, Vector3f axis);
ENGINE_API Matrix4f mat4f_scaled(Vector3f scale);

ENGINE_API Matrix4f mat4f_perspective(float fovy_radians, float aspect, float near_z, float far_z);
ENGINE_API Matrix4f mat4f_orthographic(float left, float right, float top, float bottom, float near, float far);

ENGINE_API Matrix4f mat4f_lookat(Vector3f eye, Vector3f center, Vector3f up);

ENGINE_API void mat4f_debug_print(Matrix4f m);
ENGINE_API void vec4f_debug_print(Vector4f v);
ENGINE_API void vec3f_debug_print(Vector3f v);

#endif /* CMATH_H_ */
