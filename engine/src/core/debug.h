#pragma once

#include <stdlib.h>
#include "logger.h"

#if defined(_MSC_VER)
	#define debug_break() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
	#define debug_break() __builtin_trap()
#else
	#include <signal.h>
	#define debug_break() raise(SIGTRAP)
#endif

#ifdef NDEBUG
	#define ASSERT(condition) ((void)0)
	#define ASSERT_MESSAGE(condition, message) ((void)0)
	#define ASSERT_FORMAT(condition, fmt, ...) ((void)0)
#else
	#define ASSERT(condition)                                    \
		do {                                                     \
			if (!(condition)) {                                  \
				LOG_ERROR("Assertion failed: [%s]", #condition); \
				debug_break();                                   \
				abort();                                         \
			}                                                    \
		} while (0)
	#define ASSERT_MESSAGE(condition, message)                                 \
		do {                                                                   \
			if (!(condition)) {                                                \
				LOG_ERROR("Assertion failed: [%s] | %s", #condition, message); \
				debug_break();                                                 \
				abort();                                                       \
			}                                                                  \
		} while (0)
	#define ASSERT_FORMAT(condition, fmt, ...)             \
		do {                                               \
			if (!(condition)) {                            \
				LOG_ERROR("Assertion failed: [%s] | " fmt, \
					#condition, __VA_ARGS__);              \
				debug_break();                             \
				abort();                                   \
			}                                              \
		} while (0)
#endif
