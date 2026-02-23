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

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define clamp(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

#define FLAG_GET(flags, flag) ((flags & flag) == flag)

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
typedef struct { float32 x, y; } float2;
STATIC_ASSERT(sizeof(float2) == 2 * sizeof(float));
typedef struct { float32 x, y, z; } float3;
STATIC_ASSERT(sizeof(float3) == 3 * sizeof(float));
typedef struct alignas(16) { float32 x, y, z, w; } float4;
STATIC_ASSERT(sizeof(float4) == 4 * sizeof(float));

typedef uint32_t uint32;
typedef struct { uint32 x, y; } uint2;
typedef struct { uint32 x, y, z; } uint3;
typedef struct { uint32 x, y, z, w; } uint4;

typedef int32_t int32;
typedef struct { int32 x, y; } int2;
typedef struct { int32 x, y, z; } int3;
typedef struct { int32 x, y, z, w; } int4;
// clang-format on

typedef uint32_t Flag;

// TODO: Move this?
typedef struct {
	size_t size;
	char *content;
} FileContent;
