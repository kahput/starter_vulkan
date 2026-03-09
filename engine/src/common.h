#pragma once

#include <stdarg.h>
#include <stdbool.h>
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
	#define alignof(type) offsetof(struct { char c; type member; }, member)
#endif

#if defined(_MSC_VER)
	#define alignas(X) __declspec(align(X))
#else
	#define alignas(X) __attribute((aligned(X)))
#endif

#define sizeof_member(type, member) (sizeof(((type *)0)->member))
#define countof(array) (sizeof(array) / sizeof((array)[0]))
#define indexof(array, ptr) (uint32_t)(ptr - array)

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

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

// Types
// clang-format off
typedef float float32;
typedef struct { float32 x, y; } float32_2;
typedef struct { float32 x, y, z; } float32_3;
typedef struct alignas(16) { float32 x, y, z, w; } float32_4;

typedef double float64;
typedef struct { float64 x, y; } float64_2;
typedef struct { float64 x, y, z; } float64_3;
typedef struct alignas(16) { float64 x, y, z, w; } float64_4;

typedef uint32_t uint32;
typedef struct { uint32 x, y; } uint32_2;
typedef struct { uint32 x, y, z; } uint32_3;
typedef struct { uint32 x, y, z, w; } uint32_4;

typedef int32_t int32;
typedef struct { int32 x, y; } int32_2;
typedef struct { int32 x, y, z; } int32_3;
typedef struct { int32 x, y, z, w; } int32_4;
// clang-format on

typedef uint32_t Flag;
