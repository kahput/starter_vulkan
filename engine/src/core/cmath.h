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
	float elements[3 * 3];
} float3x3;
typedef struct {
	float elements[4 * 4];
} float4x4;
typedef float4 quat4;

// --- scalar ---
INLINE float radians(float degree) { return degree * DEG2RAD; }
INLINE float degrees(float radians) { return radians * RAD2DEG; }

float randf_range(float min, float max);
uint32_t randu_range(uint32_t min, uint32_t max);
int32_t randi_range(int32_t min, int32_t max);

INLINE uint32_t clamp1u(uint32_t value, uint32_t lo, uint32_t hi) { return value < lo ? lo : (value > hi ? hi : value); }
INLINE int32_t clamp1s(int32_t value, int32_t lo, int32_t hi) { return value < lo ? lo : (value > hi ? hi : value); }
INLINE int32_t abs1i(int32_t value) { return value < 0 ? -value : value; }

INLINE float clampf(float value, float min, float max) { return value < min ? min : (value > max ? max : value); }
INLINE float signf(float value) { return (value > 0.0f) - (value < 0.0f); }
INLINE float sqf(float v) { return v * v; }
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

INLINE int32_t min1i(int32_t a, int32_t b) { return a < b ? a : b; }
INLINE int32_t max1i(int32_t a, int32_t b) { return a > b ? a : b; }

INLINE float smoothstepf(float edge0, float edge1, float v) {
	v = clampf((v - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return v * v * (3.0f - 2.0f * v);
}

// --- float2 ---
INLINE void store2(float2 src, float dst[2]) { dst[0] = src.x, dst[1] = src.y; }
INLINE float2 load2(const float v[2]) { return (float2){ v[0], v[1] }; }
#define spread2(v) v.x, v.y
#define as2(v, T) ((T){ v.x, v.y })

INLINE bool eq2(float2 a, float2 b) { return eqf(a.x, b.x) && eqf(a.y, b.y); }
INLINE float2 neg2(float2 v) { return (float2){ -v.x, -v.y }; }

INLINE float2 add2(float2 a, float2 b) { return (float2){ a.x + b.x, a.y + b.y }; }
INLINE float2 sub2(float2 a, float2 b) { return (float2){ a.x - b.x, a.y - b.y }; }
INLINE float2 mul2(float2 a, float2 b) { return (float2){ a.x * b.x, a.y * b.y }; }
INLINE float2 scale2(float2 v, float s) { return (float2){ v.x * s, v.y * s }; }
INLINE float dot2(float2 a, float2 b) { return a.x * b.x + a.y * b.y; }
INLINE float2 madd2(float2 a, float2 b, float2 c) { return (float2){ fmaf(b.x, c.x, a.x), fmaf(b.y, c.x, a.y) }; }
INLINE float2 sadd2(float2 a, float2 b, float c) { return (float2){ fmaf(b.x, c, a.x), fmaf(b.y, c, a.y) }; }
INLINE float2 inverse2(float2 v) { return (float2){ 1.0f / v.x, 1.0f / v.y }; }

INLINE float lensq2(float2 v) { return dot2(v, v); }
INLINE float len2(float2 v) { return sqrtf(dot2(v, v)); }
INLINE float dist2(float2 a, float2 b) { return len2(sub2(b, a)); }
INLINE float distsq2(float2 a, float2 b) { return lensq2(sub2(b, a)); }

INLINE float2 min2(float2 a, float2 b) { return (float2){ minf(a.x, b.x), minf(a.y, b.y) }; }
INLINE float2 max2(float2 a, float2 b) { return (float2){ maxf(a.x, b.x), maxf(a.y, b.y) }; }
INLINE float hmin2(float2 v) { return minf(v.x, v.y); }
INLINE float hmax2(float2 v) { return maxf(v.x, v.y); }

INLINE float2 abs2(float2 v) { return (float2){ fabsf(v.x), fabsf(v.y) }; }
INLINE int2 ifloor2(float2 v) { return (int2){ (int32_t)v.x, (int32_t)v.y }; }
INLINE float2 clamp2(float2 v, float min, float max) { return (float2){ clampf(v.x, min, max), clampf(v.y, min, max) }; }

INLINE float2 normalize2(float2 v) {
	float lengthsq = lensq2(v);
	if (lengthsq < EPSILON * EPSILON)
		return (float2){ 0 };

	return scale2(v, 1.0f / sqrtf(lengthsq));
}
INLINE float2 proj2(float2 v, float2 axis) { return scale2(axis, dot2(axis, v)); }
INLINE float2 lerp2(float2 start, float2 end, float t) { return (float2){ lerpf(start.x, end.x, t), lerpf(start.y, end.y, t) }; }
INLINE float2 center2(float2 a, float2 b) { return scale2(add2(a, b), 0.5f); }
INLINE float2 rotate2(float2 v, float rad) {
	float c = cosf(rad);
	float s = sinf(rad);
	return (float2){
		v.x * c - v.y * s,
		v.x * s + v.y * c
	};
}

// --- float3 ---
INLINE float3 load3(const float v[3]) { return (float3){ v[0], v[1], v[2] }; }
INLINE float3 store3(float dst[3], float3 src) { return dst[0] = src.x, dst[1] = src.y, dst[2] = src.z, load3(dst); }
#define spread3(v) (v).x, (v).y, (v).z
#define as3(v, T) ((T){ (v).x, (v).y, (v).z })

INLINE bool eq3(float3 a, float3 b) { return eqf(a.x, b.x) && eqf(a.y, b.y) && eqf(a.z, b.z); }
INLINE float3 neg3(float3 v) { return (float3){ -v.x, -v.y, -v.z }; }

INLINE float3 add3(float3 a, float3 b) { return (float3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
INLINE float3 sub3(float3 a, float3 b) { return (float3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
INLINE float3 mul3(float3 a, float3 b) { return (float3){ a.x * b.x, a.y * b.y, a.z * b.z }; }
INLINE float3 scale3(float3 v, float s) { return (float3){ v.x * s, v.y * s, v.z * s }; }
INLINE float dot3(float3 a, float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
INLINE float3 madd3(float3 a, float3 b, float3 c) { return (float3){ fmaf(b.x, c.x, a.x), fmaf(b.y, c.x, a.y), fmaf(b.z, c.x, a.z) }; }
INLINE float3 sadd3(float3 a, float3 b, float c) { return madd3(a, b, float3(c)); }

INLINE float3 cross3(float3 a, float3 b) { return (float3){ a.y * b.z - b.y * a.z, a.z * b.x - b.z * a.x, a.x * b.y - b.x * a.y }; }
INLINE float lensq3(float3 v) { return dot3(v, v); }
INLINE float len3(float3 v) { return sqrtf(dot3(v, v)); }
INLINE float dist3(float3 a, float3 b) { return len3(sub3(b, a)); }
INLINE float distsq3(float3 a, float3 b) { return lensq3(sub3(b, a)); }

INLINE float3 normalize3(float3 v) {
	float length = len3(v);
	if (length < EPSILON)
		return (float3){ 0 };

	return scale3(v, 1.0f / length);
}

INLINE float3 min3(float3 a, float3 b) { return (float3){ minf(a.x, b.x), minf(a.y, b.y), minf(a.z, b.z) }; }
INLINE float3 max3(float3 a, float3 b) { return (float3){ maxf(a.x, b.x), maxf(a.y, b.y), maxf(a.z, b.z) }; }
INLINE float hmin3(float3 v) { return minf(v.x, minf(v.y, v.z)); }
INLINE float hmax3(float3 v) { return maxf(v.x, maxf(v.y, v.z)); }

INLINE float3 abs3(float3 v) { return (float3){ fabsf(v.x), fabsf(v.y), fabsf(v.z) }; }
INLINE float3 clamp3(float3 v, float min, float max) { return (float3){ clampf(v.x, min, max), clampf(v.y, min, max), clampf(v.z, min, max) }; }
INLINE float3 approach3(float3 start, float3 end, float step) {
	float3 vd = sub3(end, start);
	float len = len3(vd);

	if (len <= step || len < EPSILON)
		return end;

	return add3(start, scale3(vd, step / len));
}
INLINE float3 lerp3(float3 start, float3 end, float t) { return madd3(start, sub3(end, start), float3(t)); }
INLINE float3 orthobasis3(float3 normal, float3 *right, float3 *up) {
	normal = normalize3(normal);
	*right = normalize3(cross3(normal, fabsf(dot3(FLOAT3_UP, normal)) >= 0.99f ? FLOAT3_BACKWARD : FLOAT3_UP));
	*up = cross3(normal, *right);
	return normal;
}

INLINE float angle3(float3 a, float3 b) { return acosf(clampf(dot3(normalize3(a), normalize3(b)), -1.0f, 1.0f)); }

// --- float4 & quaternions ---
INLINE void store4(float dst[4], float4 src) { dst[0] = src.x, dst[1] = src.y, dst[2] = src.z, dst[3] = src.w; }
INLINE float4 load4(const float v[4]) { return (float4){ .x = v[0], .y = v[1], .z = v[2], .w = v[3] }; }
#define spread4(v) v.x, v.y, v.z, v.w
#define as4(v, T) ((T){ v.x, v.y, v.z, v.w })

INLINE bool eq4(float4 a, float4 b) { return eqf(a.x, b.x) && eqf(a.y, b.y) && eqf(a.z, b.z) && eqf(a.w, b.w); }

INLINE float4 add4(float4 a, float4 b) { return (float4){ a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w }; }
INLINE float4 sub4(float4 a, float4 b) { return (float4){ a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w }; }
INLINE float4 mul4(float4 a, float4 b) { return (float4){ a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w }; }
INLINE float4 scale4(float4 v, float s) { return (float4){ v.x * s, v.y * s, v.z * s, v.w * s }; }
INLINE float dot4(float4 a, float4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
INLINE float4 neg4(float4 v) { return (float4){ -v.x, -v.y, -v.z, -v.w }; }

INLINE float lensq4(float4 v) { return dot4(v, v); }
INLINE float len4(float4 v) { return sqrtf(dot4(v, v)); }
INLINE float4 clamp4(float4 v, float min, float max) { return (float4){ clampf(v.x, min, max), clampf(v.y, min, max), clampf(v.z, min, max), clampf(v.z, min, max) }; }

INLINE float4 normalize4(float4 v) {
	float length = len4(v);
	if (length < EPSILON)
		return (float4){ 0 };

	return scale4(v, 1.0f / length);
}

INLINE quat4 identity4q(void) { return (quat4){ 0.0f, 0.0f, 0.0f, 1.0f }; }
float3 euler4q(quat4 quat);
quat4 axisangle4q(float3 axis, float angle);
quat4 slerp4q(quat4 q, quat4 p, float t);

// clang-format off
// https://en.wikipedia.org/wiki/Quaternion#Hamilton_product
INLINE quat4 mul4q(quat4 q, quat4 p) {
    return (quat4){
        q.w*p.x + q.x*p.w + q.y*p.z - q.z*p.y,
        q.w*p.y - q.x*p.z + q.y*p.w + q.z*p.x,
        q.w*p.z + q.x*p.y - q.y*p.x + q.z*p.w,
        q.w*p.w - q.x*p.x - q.y*p.y - q.z*p.z,
    };
}
INLINE quat4 conjugate4q(quat4 q) { return (quat4){ -q.x, -q.y, -q.z, q.w }; }

// https://raw.org/math/algebra/vector-rotation-using-quaternions/
INLINE float3 rotate4q(quat4 q, float3 v) {
    float3 u = { q.x, q.y, q.z }, t = scale3(cross3(u, v), 2.0f);
    return add3(v, add3(scale3(t, q.w), cross3(u, t))); // v + t * q.w + cross(u, t);
}
// clang-format on

// --- float2x2 ---
// clang-format off
INLINE float2x2 rot2x2(float rad) {
	float c = cosf(rad);
	float s = sinf(rad);
	return (float2x2){{
        [0] = c, [2] = -s,
        [1] = s, [3] = c,
    }};
}
// clang-format on
INLINE float2 mul2x2v(float2x2 m, float2 v) { return (float2){ m.elements[0] * v.x + m.elements[2] * v.y, m.elements[1] * v.x + m.elements[3] * v.y }; }
INLINE float det2x2(float2x2 m) { return m.elements[0] * m.elements[3] - m.elements[2] * m.elements[1]; }

INLINE float2x2 columns2x2(float2 right, float2 up) { return (float2x2){ { right.x, right.y, up.x, up.y } }; }
INLINE float2x2 diagonal2x2(float2 d) { return (float2x2){ { d.x, 0.0f, 0.0, d.y } }; }

// --- float3x3 ---
// clang-format off
INLINE float3x3 mul3x3(float3x3 lhs, float3x3 rhs) {
#define DOT(row, col) (                                     \
                                                            \
	lhs.elements[row + 0 * 3] * rhs.elements[col * 3 + 0] + \
	lhs.elements[row + 1 * 3] * rhs.elements[col * 3 + 1] + \
	lhs.elements[row + 2 * 3] * rhs.elements[col * 3 + 2]   \
                                                            \
)
	return (float3x3){{
          [0] = DOT(0, 0), [3] = DOT(0, 1), [6] = DOT(0, 2),
          [1] = DOT(1, 0), [4] = DOT(1, 1), [7] = DOT(1, 2),
          [2] = DOT(2, 0), [5] = DOT(2, 1), [8] = DOT(2, 2)
	}};
#undef DOT
}

INLINE float3 mul3x3v(float3x3 m, float3 v) {
	return (float3){
		m.elements[0] * v.x + m.elements[3] * v.y + m.elements[6] * v.z,
		m.elements[1] * v.x + m.elements[4] * v.y + m.elements[7] * v.z,
		m.elements[2] * v.x + m.elements[5] * v.y + m.elements[8] * v.z,
	};
}
INLINE float det3x3(float3x3 m) {
	return (
		m.elements[0] * det2x2((float2x2){ { m.elements[4], m.elements[5], m.elements[7], m.elements[8] } }) -
		m.elements[3] * det2x2((float2x2){ { m.elements[1], m.elements[2], m.elements[7], m.elements[8] } }) +
		m.elements[6] * det2x2((float2x2){ { m.elements[1], m.elements[2], m.elements[4], m.elements[5] } }) //
	);
}

INLINE float3x3 affine3x3(float2x2 m, float2 t) {
    return (float3x3){{
        [0] = m.elements[0], [3] = m.elements[2], [6] = t.x,
        [1] = m.elements[1], [4] = m.elements[3], [7] = t.y,
        [2] = 0.0,           [5] = 0.0,           [8] = 1.0,
    }};
}

INLINE float3x3 columns3x3(float3 r, float3 u, float3 f) {
	return (float3x3){{
	  [0] = r.x,  [3] = u.x,  [6] = f.x, 
	  [1] = r.y,  [4] = u.y,  [7] = f.y, 
	  [2] = r.z,  [5] = u.z,  [8] = f.z,  
	}};
}

INLINE float3x3 rows3x3(float3 r, float3 u, float3 f) {
	return (float3x3){{
	  [0] = r.x,  [3] = r.y,  [6] = r.z, 
	  [1] = u.x,  [4] = u.y,  [7] = u.z, 
	  [2] = f.x,  [5] = f.y,  [8] = f.z,  
	}};
}

INLINE float3x3 transpose3x3(float3x3 m) {
    return (float3x3){{
        [0] = m.elements[0], [3] = m.elements[1], [6] = m.elements[2],
        [1] = m.elements[3], [4] = m.elements[4], [7] = m.elements[5],
        [2] = m.elements[6], [5] = m.elements[7], [8] = m.elements[8]
    }};
}
// clang-format on

float3x3 axisangle3x3(float3 axis, float angle);

INLINE float2 xform2p(float3x3 m, float2 p) { return xy3(mul3x3v(m, make3_from2(p, 1.0f))); }
INLINE float2 xform2v(float3x3 m, float2 d) { return xy3(mul3x3v(m, make3_from2(d, 0.0f))); }

INLINE float3x3 diagonal3x3(float3 v) {
	return (float3x3){ .elements[0] = v.x, .elements[4] = v.y, .elements[8] = v.z };
}
INLINE float3x3 identity3x3(void) { return diagonal3x3(splat3(1.0f)); }

// --- float4x4 ---
bool eq4x4(float4x4 lhs, float4x4 rhs);
float4x4 mul4x4(float4x4 lhs, float4x4 rhs);
float4 mul4x4v(float4x4 m, float4 v);

INLINE float3 xform3p(float4x4 m, float3 p) { return xyz4(mul4x4v(m, make4_from3(p, 1.0f))); }
INLINE float3 xform3v(float4x4 m, float3 d) { return xyz4(mul4x4v(m, make4_from3(d, 0.0f))); }

INLINE float4x4 diagonal4x4(float4 v) {
	return (float4x4){ .elements[0] = v.x, .elements[5] = v.y, .elements[10] = v.z, .elements[15] = v.w };
}

#define IDENTITY4x4 { { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f } }
INLINE float4x4 identity4x4(void) { return diagonal4x4(splat4(1.0f)); }

// clang-format off
// https://en.wikipedia.org/wiki/Quaternions_and_spatial_rotation#Quaternion-derived_rotation_matrix
INLINE float4x4 rot4x4(quat4 q) {
	float x = q.x, y = q.y, z = q.z, w = q.w;
	float xx = x * x, yy = y * y, zz = z * z;
	float xy = x * y, xz = x * z, yz = y * z;
	float wx = w * x, wy = w * y, wz = w * z;
	float s = 1.0f; // NOTE: assumes q is normalized

	return (float4x4){{
	  [0]  = 1.0f-2.0f*s*(yy+zz), [4]  = 2.0f*s*(xy-wz),      [8 ] = 2.0f*s*(xz+wy),      [12] = 0.0f,
	  [1]  = 2.0f*s*(xy+wz),      [5]  = 1.0f-2.0f*s*(xx+zz), [9 ] = 2.0f*s*(yz-wx),      [13] = 0.0f,
	  [2]  = 2.0f*s*(xz-wy),      [6]  = 2.0f*s*(yz+wx),      [10] = 1.0f-2.0f*s*(xx+yy), [14] = 0.0f,
	  [3]  = 0.0f,                [7]  = 0.0f,                [11] = 0.0f,                [15] = 1.0f
	}};
}
INLINE float4x4 translation4x4(float3 translation) {
    return (float4x4){{
	  [0] = 1.0f, [4] = 0.0f, [8] =  0.0f, [12] = translation.x,
	  [1] = 0.0f, [5] = 1.0f, [9] =  0.0f, [13] = translation.y,
	  [2] = 0.0f, [6] = 0.0f, [10] = 1.0f, [14] = translation.z,
	  [3] = 0.0f, [7] = 0.0f, [11] = 0.0f, [15] = 1.0f
	}};
}
INLINE float4x4 scaling4x4(float3 scale) {
    return (float4x4){{
	  [0] = scale.x, [4] = 0.0f,    [8] =  0.0f,    [12] = 0.0f,
	  [1] = 0.0f,    [5] = scale.y, [9] =  0.0f,    [13] = 0.0f,
	  [2] = 0.0f,    [6] = 0.0f,    [10] = scale.z, [14] = 0.0f,
	  [3] = 0.0f,    [7] = 0.0f,    [11] = 0.0f,    [15] = 1.0f
	}};
}

INLINE float4x4 affine4x4(float3x3 m, float3 translation) {
	return (float4x4){{
	  [0] = m.elements[0], [4] = m.elements[3], [8] =  m.elements[6], [12] = translation.x,
	  [1] = m.elements[1], [5] = m.elements[4], [9] =  m.elements[7], [13] = translation.y,
	  [2] = m.elements[2], [6] = m.elements[5], [10] = m.elements[8], [14] = translation.z,
	  [3] = 0.0f,          [7] = 0.0f,          [11] = 0.0f,          [15] = 1.0f
	}};
}

INLINE float4x4 trs4x4_quat(float3 translation, quat4 rotation, float3 scale) {
	return mul4x4(
		translation4x4(translation),
		mul4x4(rot4x4(rotation), scaling4x4(scale)) //
	);
}
INLINE float4x4 columns4x4(float4 a, float4 b, float4 c, float4 d) {
	return (float4x4){{
	  [0] = a.x,  [4] = b.x,  [8 ] = c.x, [12] = d.x,
	  [1] = a.y,  [5] = b.y,  [9 ] = c.y, [13] = d.y,
	  [2] = a.z,  [6] = b.z,  [10] = c.z, [14] = d.z,
      [3] = a.w,  [7] = b.w,  [11] = c.w, [15] = d.w,
	}};
}

INLINE float4x4 rows4x4(float4 a, float4 b, float4 c, float4 d) {
	return (float4x4){{
	  [0] = a.x,  [4] = a.y,  [8 ] = a.z, [12] = a.w,
	  [1] = b.x,  [5] = b.y,  [9 ] = b.z, [13] = b.w,
	  [2] = c.x,  [6] = c.y,  [10] = c.z, [14] = c.w,
      [3] = d.x,  [7] = d.y,  [11] = d.z, [15] = d.w,
	}};
}

INLINE float4x4 transpose4x4(float4x4 m) {
    return (float4x4){{
        [0] = m.elements[0 ], [4] = m.elements[1],  [8 ] = m.elements[2 ], [12] = m.elements[3 ],
        [1] = m.elements[4 ], [5] = m.elements[5],  [9 ] = m.elements[6 ], [13] = m.elements[7 ],
        [2] = m.elements[8 ], [6] = m.elements[9],  [10] = m.elements[10], [14] = m.elements[11],
        [3] = m.elements[12], [7] = m.elements[13], [11] = m.elements[14], [15] = m.elements[15],
    }};
}

// clang-format on
float4x4 trs4x4_euler(float3 position, float3 rotation, float3 scale);

float4x4 perspective(float radians_fovy, float aspect, float near_z, float far_z);
float4x4 orthographic(float left, float right, float top, float bottom, float near, float far);

INLINE float4x4 lookat(float3 eye, float3 center, float3 up) {
	float3 camera_forward = normalize3(sub3(center, eye));
	float3 camera_right = normalize3(cross3(camera_forward, up));
	float3 camera_up = cross3(camera_right, camera_forward);

	return affine4x4(
		rows3x3(camera_right, camera_up, neg3(camera_forward)),
		float3(-dot3(camera_right, eye), -dot3(camera_up, eye), dot3(camera_forward, eye)) //
	);
}

INLINE float4 rgba_from_hsv(float h, float s, float v) {
	h = fmodf(maxf(h, 0.0f), 360.0) / 360.0, s = CLAMP(s, 0.0, 1.0), v = CLAMP(v, 0.0, 1.0);
	float max_rgb = v;
	float range_rgb = v * s;
	float min_rgb = max_rgb - range_rgb;

	float r = fabsf(h * 6.0f - 3.0f) - 1.0f;
	float g = 2.0f - fabsf(h * 6.0f - 2.0f);
	float b = 2.0f - fabsf(h * 6.0f - 4.0f);

	return (float4){
		.x = clampf(r, 0.0f, 1.0f) * range_rgb + min_rgb,
		.y = clampf(g, 0.0f, 1.0f) * range_rgb + min_rgb,
		.z = clampf(b, 0.0f, 1.0f) * range_rgb + min_rgb,
		.w = 1.0f
	};
}

#endif /* CMATH_H_ */
