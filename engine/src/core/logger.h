#pragma once

#include "common.h"

typedef enum {
	LOG_LEVEL_TRACE = 0,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_FATAL
} LogLevel;

#ifndef NDEBUG
	#define LOG_TRACE(...) logger_log(LOG_LEVEL_TRACE, __FILE__, __LINE__, __VA_ARGS__)
	#define LOG_DEBUG(...) logger_log(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
	#define LOG_INFO(...) logger_log(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
	#define LOG_WARN(...) logger_log(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#else
	#define LOG_TRACE(...)
	#define LOG_DEBUG(...)
	#define LOG_INFO(...)
	#define LOG_WARN(...)
#endif
#define LOG_ERROR(...) logger_log(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) logger_log(LOG_LEVEL_FATAL, __FILE__, __LINE__, __VA_ARGS__)

const char *logger_level_to_string(LogLevel level);
void logger_set_level(LogLevel level);
void logger_set_quiet(bool enable);

void logger_indent(void);
void logger_dedent(void);

ENGINE_API void logger_log(LogLevel level, const char *file, int line, const char *fmt, ...);
