#pragma once

#include "os.h"
#include "common.h"
#include "logger.h"

#if defined(_MSC_VER)
	#define debug_break() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
	#define debug_break() __builtin_trap()
#else
	#include <signal.h>
	#define debug_break() raise(SIGTRAP)
#endif

#ifdef DEBUG_BUILD
	#define ASSERT(condition)                         \
		do {                                          \
			if (!(condition)) {                       \
				log_error("ASSERT [%s]", #condition); \
				debug_break();                        \
				os_abort();                           \
			}                                         \
		} while (0)
	#define ASSERT_MESSAGE(condition, message)                     \
		do {                                                       \
			if (!(condition)) {                                    \
				log_error("ASSERT [%s]\n%s", #condition, message); \
				debug_break();                                     \
				os_abort();                                        \
			}                                                      \
		} while (0)
	#define ASSERT_FORMAT(condition, fmt, ...) \
		do {                                   \
			if (!(condition)) {                \
				log_error("ASSERT [%s]\n" fmt, \
					#condition, __VA_ARGS__);  \
				debug_break();                 \
				os_abort();                    \
			}                                  \
		} while (0)

	#define INVALID_PATH ASSERT(!"InvalidCodePath")
	#define NOT_IMPLEMENTED ASSERT(!"NotImplemented")
#else
	#define ASSERT(condition) ((void)0)
	#define ASSERT_MESSAGE(condition, message) ((void)0)
	#define ASSERT_FORMAT(condition, fmt, ...) ((void)0)
	#define INVALID_PATH ((void)0)
	#define NOT_IMPLEMENTED ((void)0)
#endif
