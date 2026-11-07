#pragma once

#include "common.h"

typedef enum {
	LOG_TRACE = 0,
	LOG_DEBUG,
	LOG_INFO,
	LOG_WARN,
	LOG_ERROR,
	LOG_FATAL,

	LOG_MAX,
} LogLevel;

#ifdef DEBUG_BUILD
	#define log_trace(...) log_write(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
	#define log_debug(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
	#define log_info(...) log_write(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
	#define log_warn(...) log_write(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#else
	#define log_trace(...)
	#define log_debug(...)
	#define log_info(...)
	#define log_warn(...)
#endif
#define log_error(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_fatal(...) log_write(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

const char *log_level_to_string(LogLevel level);
void log_level(LogLevel level);

ENGINE_API void log_write(LogLevel level, const char *file, uint64_t line, const char *fmt, ...) FORMAT_ATTRIBUTE(4);
