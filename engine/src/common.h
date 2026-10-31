#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#if defined(__EMSCRIPTEN__) || defined(__wasm__)
	#define WEB_BUILD
#elif defined(_WIN32)
	#define WINDOWS_BUILD
#elif defined(__linux__)
	#define LINUX_BUILD
/* #elif defined(__APPLE__) && defined(__MACH__) */
#else
	#error Unsupported backend
#endif

#if defined(__linux__) || defined(__APPLE__)
	#define ENGINE_API __attribute__((visibility("default")))
#elif defined(_WIN32)
	#ifdef ENGINE_EXPORT
		#define ENGINE_API __declspec(dllexport)
	#else
		#define ENGINE_API __declspec(dllimport)
	#endif
#else
	#define ENGINE_API
#endif

#define offsetof(T, m) ((uint64_t)&(((T *)0)->m))

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
	#define alignof(type) _Alignof(type)
#elif defined(__GNUC__) || defined(__clang__)
	#define alignof(type) __alignof__(type)
#elif defined(_MSC_VER)
	#define alignof(type) __alignof(type)
#else
	#define alignof(T) (size_t)(&((struct {  char byte; T offset; } *)0)->offset)
#endif

#if defined(_MSC_VER)
	#define alignas(X) __declspec(align(X))
#else
	#define alignas(X) __attribute((aligned(X)))
#endif

#define FN_NAME __func__

#if defined(_MSC_VER)
	#define INLINE static __forceinline
	#define PACK_BEGIN(gran) __pragma(pack(push, (gran)))
	#define PACK_END __pragma(pack(pop))
#define FORMAT_ATTRIBUTE
#else
	#define INLINE static inline __attribute((always_inline))
	#define PACK_BEGIN(gran) __attribute__((__packed__))
	#define PACK_END
#define FORMAT_ATTRIBUTE(fmt_index) __attribute__((format(printf, fmt_index, fmt_index + 1)))
#endif

#define sizeof_member(type, member) (sizeof(((type *)0)->member))
#define countof(array) (sizeof(array) / sizeof((array)[0]))
#define indexof(array, ptr) (uint32_t)(ptr - array)
#define containerof(ptr, T, member) ((T *)((uint8_t *)ptr - offsetof(T, member)))
#define arr(T, ...) (T[]){ __VA_ARGS__ }, sizeof (T[]){ __VA_ARGS__ } / sizeof(T)
#define arr_n(T, n) (T[(n)]){ (T){ 0 } }, (n)
#define swapptr(a, b)      \
	do {                   \
		void *_temp = (a); \
		(a) = (b);         \
		(b) = _temp        \
	} while (0)
#define swap(a, b, T)  \
	do {               \
		T _temp = (a); \
		(a) = (b);     \
		(b) = _temp;   \
	} while (0)

#define memory_copy(dst, src, size) memcpy((dst), (src), (size))
#define memory_set(dst, byte, size) memset((dst), (byte), (size))
#define memory_compare(a, b, size) memcmp((a), (b), (size))

#define memory_copy_struct(d, s) memory_copy((d), (s), sizeof(*(d)))
#define memory_copy_array(d, s) memory_copy((d), (s), sizeof(s))
#define memory_copy_count(d, s, c) memory_copy((d), (s), sizeof(*(s)) * (c))

#define memory_zero(s, z) memory_set((s), 0, (z))
#define memory_zero_struct(s) memory_zero(&(s), sizeof((s)))
#define memory_zero_array(a) memory_zero((a), sizeof(a))
#define memory_zero_count(m, c) memory_zero((m), sizeof(*(m)) * (c))

#define memory_equals(a, b, z) (memory_compare((a), (b), (z)) == 0)
#define memory_equals_struct(a, b) memory_equals((a), (b), sizeof(*(a)))
#define memory_equals_array(a, b) memory_equals((a), (b), sizeof(a))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define CLAMP(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

#define BIT(b) (1 << (b))
#define has_flag(flags, flag) (((flags) & (flag)) == (flag))

#define STATIC_ASSERT_PASTE_(a, b) a##b
#define STATIC_ASSERT_PASTE(a, b) STATIC_ASSERT_PASTE_(a, b)

#define STRINGIFY(v) #v
#define STATIC_ASSERT(COND) typedef char STATIC_ASSERT_PASTE(static_assertion_failed_at_line_, __LINE__)[(COND) ? 1 : -1]
#define ENUM_STRING_TABLE_ENTRY(prefix, value) [prefix##_##value] = comp8(#value)

#define SELECT2(_1, _2, FN, ...) FN
#define SELECT3(_1, _2, _3, FN, ...) FN
#define SELECT4(_1, _2, _3, _4, FN, ...) FN

#define META(...)

#define KB(bytes) ((uint64_t)(bytes) * 1000ULL)
#define MB(bytes) ((KB(bytes)) * 1000ULL)
#define GB(bytes) ((MB(bytes)) * 1000ULL)

#define KiB(bytes) ((uint64_t)(bytes) * 1024ULL)
#define MiB(bytes) ((KiB(bytes)) * 1024ULL)
#define GiB(bytes) ((MiB(bytes)) * 1024ULL)

INLINE uint64_t alignup(uint64_t value, uint64_t alignment) {
	return ((value + (alignment - 1)) & ~(alignment - 1));
}
INLINE uint64_t hash64(void *memory, size_t size) {
	uint64_t h = 0x100;
	for (size_t i = 0; i < size; i++) {
		h ^= ((uint8_t *)memory)[i];
		h *= 1111111111111111111;
	}
	return h;
}

// From https://stackoverflow.com/a/5889254
INLINE uint64_t hash64_combine(uint64_t lhs, uint64_t rhs) {
	lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
	return lhs;
}

#define hash_struct(s) hash64(&(s), sizeof((s)))
#define hash_array(array) hash64((array), sizeof((array)))
#define hash_count(memory, count) hash64((memory), sizeof(*(memory)) * (count))

// Types
// clang-format off
typedef struct { float x, y; } float2;
typedef struct { float x, y, z; } float3;
typedef struct alignas(16) { float x, y, z, w; } float4;

typedef struct { double x, y; } double2;
typedef struct { double x, y, z; } double3;
typedef struct alignas(16) { double x, y, z, w; } double4;

typedef struct { uint32_t x, y; } uint2;
typedef struct { uint32_t x, y, z; } uint3;
typedef struct { uint32_t x, y, z, w; } uint4;

typedef struct { int16_t x, y; } short2;

typedef struct { int32_t x, y; } int2;
typedef struct { int32_t x, y, z; } int3;
typedef struct { int32_t x, y, z, w; } int4;

// make
INLINE float2 splat2(float v) { return (float2){ v, v }; }
INLINE float2 make2(float x, float y) { return (float2){ x, y }; }

INLINE float3 splat3(float v) { return (float3){ v, v, v }; }
INLINE float3 make3_from2(float2 v, float z) { return (float3){ v.x, v.y, z }; }
INLINE float3 make3(float x, float y, float z) { return (float3){ x, y, z }; }

INLINE float4 splat4(float v) { return (float4){ v, v, v, v }; }
INLINE float4 make4_from3(float3 v, float w) { return (float4){ v.x, v.y, v.z, w }; }
INLINE float4 make4_from2(float2 v, float z, float w) { return (float4){ v.x, v.y, z, w }; }
INLINE float4 make4(float x, float y, float z, float w) { return (float4){ x, y, z, w }; }

// truncate
INLINE float3 xyz4(float4 v) { return make3(v.x, v.y, v.z); }
INLINE float2 xy4(float4 v) { return make2(v.x, v.y); }
INLINE float2 xy3(float3 v) { return make2(v.x, v.y); }

#define float2(...) SELECT2(__VA_ARGS__, make2, splat2, _)(__VA_ARGS__)
#define float3(...) SELECT3(__VA_ARGS__, make3, make3_from2, splat3, _)(__VA_ARGS__)
#define float4(...) SELECT4(__VA_ARGS__, make4, make4_from2, make4_from3, splat4, _)(__VA_ARGS__)

#define FLOAT3_RIGHT ((float3){1.0f, 0.0f, 0.0f})
#define FLOAT3_LEFT ((float3){-1.0f, 0.0f, 0.0f})
#define FLOAT3_UP ((float3){0.0f, 1.0f, 0.0f})
#define FLOAT3_DOWN ((float3){0.0f, -1.0f, 0.0f})
#define FLOAT3_FORWARD ((float3){0.0f, 0.0f, -1.0f})
#define FLOAT3_BACKWARD ((float3){0.0f, 0.0f, 1.0f})

typedef struct { float min, max; } Interval;
typedef struct { float2 min, max; } Interval2;
typedef struct { float3 min, max; } Interval3;

typedef uint32_t RID;

typedef struct {
	float x, y, width, height;
} Rectangle;
INLINE Rectangle rect_square(float s){  return (Rectangle){ 0.0f, 0.0f, s, s }; }
INLINE Rectangle rect_wh(float w, float h) { return (Rectangle){ 0.0f, 0.0f, w, h }; }
INLINE Rectangle rect_pwh(float2 p, float w, float h) { return (Rectangle){ p.x, p.y, w, h }; }
INLINE Rectangle rect_xywh(float x, float y, float w, float h ){ return (Rectangle){ x, y, w, h }; }
#define rect(...) SELECT4(__VA_ARGS__, rect_xywh, rect_pwh, rect_wh, rect_square, _)(__VA_ARGS__)


typedef struct { uint8_t r, g, b, a; } rgba8;
INLINE rgba8 rgba8_gray(uint8_t scale) { return (rgba8){ scale, scale, scale, 255 }; }
INLINE rgba8 rgba8_gray_alpha(uint8_t scale, uint8_t alpha) { return (rgba8){ scale, scale, scale, alpha }; }
INLINE rgba8 rgba8_color(uint8_t r, uint8_t g, uint8_t b) { return (rgba8){ r, g, b, 255 }; }
INLINE rgba8 rgba8_color_alpha(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return (rgba8){ r, g, b, a }; }

#define rgba8(...) SELECT4(__VA_ARGS__, rgba8_color_alpha, rgba8_color, rgba8_gray_alpha, rgba8_gray, _)(__VA_ARGS__)
#define hex(u) (rgba8){ ((u) >> 16) & 0xFF, ((u) >> 8) & 0xFF, ((u) >> 0) & 0xFF, 0xFF }

// clang-format on
#define TRANSPARENT (rgba8){ 0 }

#define RED hex(0xFF0000)
#define ORANGE hex(0xFF7F00)
#define YELLOW hex(0xFFFF00)
#define GREEN hex(0x00FF00)
#define BLUE hex(0x0000FF)
#define INDIGO hex(0x4B0082)
#define VIOLET hex(0x8B00FF)

#define TEAL (rgba8){ 0, 128, 128, 255 }

#define WHITE (rgba8){ 255, 255, 255, 255 }
#define GRAY (rgba8){ 128, 128, 128, 255 }
#define DARK_GRAY (rgba8){ 64, 64, 64, 255 }
#define BLACK (rgba8){ 0, 0, 0, 255 }

INLINE uint32_t rgba8_as_uint32(rgba8 c) {
	return ((uint32_t)c.r) | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

INLINE rgba8 color_from_float(float r, float g, float b, float a) {
	return (rgba8){ CLAMP(r, 0.0f, 1.0f) * 255.f, CLAMP(g, 0.0f, 1.0f) * 255.f, CLAMP(b, 0.0f, 1.0f) * 255.f, CLAMP(a, 0.0f, 1.0f) * 255.f };
}

INLINE rgba8 color_from_float4(float4 rgba) {
	return (rgba8){ CLAMP(rgba.x, 0.0f, 1.0f) * 255.f, CLAMP(rgba.y, 0.0f, 1.0f) * 255.f, CLAMP(rgba.z, 0.0f, 1.0f) * 255.f, CLAMP(rgba.w, 0.0f, 1.0f) * 255.f };
}
INLINE float4 color_to_float4(rgba8 color) {
	return (float4){ color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f };
}
INLINE rgba8 color_lerp(rgba8 start, rgba8 end, float t) {
	return (rgba8){ start.r + (end.r - start.r) * t, start.g + (end.g - start.g) * t, start.b + (end.b - start.b) * t, start.a + (end.a - start.a) * t };
}

typedef enum {
	SIDE_RIGHT,
	SIDE_LEFT,

	SIDE_TOP,
	SIDE_UP = SIDE_TOP,

	SIDE_BOTTOM,
	SIDE_DOWN = SIDE_BOTTOM,

	SIDE_MAX2,

	SIDE_FRONT = SIDE_MAX2,
	SIDE_FORWARD = SIDE_FRONT,

	SIDE_BACK,
	SIDE_BACKWARD = SIDE_BACK,

	SIDE_MAX3,
} Side;

typedef enum {
	AXIS_X,
	AXIS_Y,
	AXIS_MAX2D,
	AXIS_Z = AXIS_MAX2D,
	AXIS_MAX3D,
} Axis;

typedef enum {
	AXIS_MODE_X = BIT(0),
	AXIS_MODE_Y = BIT(1),
	AXIS_MODE_Z = BIT(2),
	AXIS_MODE_XY = AXIS_MODE_X | AXIS_MODE_Y,
	AXIS_MODE_YZ = AXIS_MODE_Y | AXIS_MODE_Z,
	AXIS_MODE_ZX = AXIS_MODE_Z | AXIS_MODE_X,
	AXIS_MODE_XYZ = AXIS_MODE_X | AXIS_MODE_Y | AXIS_MODE_Z,
} AxisMode;
