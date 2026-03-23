#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

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

#define sizeof_member(type, member) (sizeof(((type *)0)->member))
#define countof(array) (sizeof(array) / sizeof((array)[0]))
#define indexof(array, ptr) (uint32_t)(ptr - array)
#define container_of(ptr, T, member) ((T *)((uint8_t *)ptr - offsetof(T, member)))

#define memory_copy(dst, src, size) memcpy((dst), (src), (size))
#define memory_set(dst, byte, size) memset((dst), (byte), (size))
#define memory_compare(a, b, size) memcmp((a), (b), (size))

#define memory_copy_struct(d, s) memory_copy((d), (s), sizeof(*(d)))
#define memory_copy_array(d, s) memory_copy((d), (s), sizeof(d))
#define memory_copy_count(d, s, c) memory_copy((d), (s), sizeof(*(d)) * (c))

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

#define FLAG_GET(flags, flag) ((flags & flag) == flag)
#define HEADER(ptr, T) ((T *)ptr - 1)
#define HEADER_SET(ptr, x, T) (*((T *)ptr - 1) = x)

#define STATIC_ASSERT_PASTE_(a, b) a##b
#define STATIC_ASSERT_PASTE(a, b) STATIC_ASSERT_PASTE_(a, b)

#define STATIC_ASSERT(COND) typedef char STATIC_ASSERT_PASTE(static_assertion_failed_at_line_, __LINE__)[(COND) ? 1 : -1]

#define KB(bytes) ((uint64_t)(bytes) * 1000ULL)
#define MB(bytes) ((KB(bytes)) * 1000ULL)
#define GB(bytes) ((MB(bytes)) * 1000ULL)

#define KiB(bytes) ((uint64_t)(bytes) * 1024ULL)
#define MiB(bytes) ((KiB(bytes)) * 1024ULL)
#define GiB(bytes) ((MiB(bytes)) * 1024ULL)

static inline uint64_t aligned_address(uint64_t address, uint64_t alignment) {
	return ((address + (alignment - 1)) & ~(alignment - 1));
}
static inline uint64_t hash64(void *memory, size_t size) {
	uint64_t h = 0x100;
	for (size_t i = 0; i < size; i++) {
		h ^= ((uint8_t *)memory)[i];
		h *= 1111111111111111111;
	}
	return h;
}

#define hash_struct(s) hash64(&(s), sizeof((s)))
#define hash_array(array) hash64((array), sizeof((array)))
#define hash_count(memory, count) hash64((memory), sizeof(*(memory)) * (count))

// Types
// clang-format off
typedef struct { float x, y; } float32x2;
typedef struct { float x, y, z; } float32x3;
typedef struct alignas(16) { float x, y, z, w; } float32x4;

typedef double float64;
typedef struct { float64 x, y; } float64_2;
typedef struct { float64 x, y, z; } float64_3;
typedef struct alignas(16) { float64 x, y, z, w; } float64_4;

typedef uint32_t uint32;
typedef struct { uint32 x, y; } uint32x2;
typedef struct { uint32 x, y, z; } uint32x3;
typedef struct { uint32 x, y, z, w; } uint32x4;

typedef uint32x2 uint2;
typedef uint32x3 uint3;
typedef uint32x4 uint4;

typedef int32_t int32;
typedef struct { int32 x, y; } int32x2;
typedef struct { int32 x, y, z; } int32x3;
typedef struct { int32 x, y, z, w; } int32x4;

typedef int32x2 int2;
typedef int32x3 int3;
typedef int32x4 int4;
// clang-format on

typedef struct {
	uint8_t *buffer;
	size_t length;
} Span;

static inline Span span_make(void *ptr, size_t length) { return (Span){ .buffer = ptr, .length = length }; }

#define span_struct(obj) ((Span){ .buffer = (uint8_t *)&(obj), .length = sizeof(obj) })
#define span_array(arr) ((Span){ .buffer = (uint8_t *)(arr), .length = sizeof(arr) })
#define span_count(p, n) ((Span){ .buffer = (uint8_t *)(p), .length = sizeof(*(p)) * (n) })
#define span_literal(lit) ((Span){ .buffer = (uint8_t *)(lit), .length = sizeof(lit) - 1 })
#define span_string(s) ((Span){ .buffer = (uint8_t *)(s).chars, .length = (s).length })

static inline Span span_subspan(Span s, size_t offset, size_t length) {
	if (offset > s.length)
		return span_make(NULL, 0);
	if (offset + length > s.length)
		length = s.length - offset;
	return span_make(s.buffer + offset, length);
}
static inline bool span_empty(Span s) { return s.length == 0; }
static inline bool span_equal(Span a, Span b) {
	if (a.length != b.length)
		return false;
	return memory_equals(a.buffer, b.buffer, a.length);
}

typedef uint32_t Flag;
