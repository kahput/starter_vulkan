#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef offsetof
	#define offsetof(type, member) (size_t)(&(((type *)0)->member))
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

#define sizeof_member(type, member) (sizeof(((type *)0)->member))
#define countof(array) (sizeof(array) / sizeof((array)[0]))

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define clamp(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))

#define FLAG_GET(flags, flag) ((flags & flag) == flag)

#define INVALID_INDEX UINT32_MAX
#define INVALID_UUID UINT64_MAX

#define STATIC_ASSERT_(COND, LINE)

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

typedef uint32_t Flag;

// TODO: Move this?
typedef struct {
	size_t size;
	char *content;
} FileContent;
