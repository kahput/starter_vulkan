#include "logger.h"

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
	LogLevel level;
	bool quiet;
	uint32_t indent;
} Logger;

static Logger g_logger = { LOG_LEVEL_TRACE, false, 0 };
static const char *g_level_strings[] = {
	"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};
static const char *g_log_level_colors[] = {
	"\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};

const char *logger_level_to_string(LogLevel level) {
	return g_level_strings[level];
}
void logger_set_level(LogLevel level) {
	g_logger.level = level;
}
void logger_set_quiet(bool enable) {
	g_logger.quiet = enable;
}

void logger_indent(void) {
	g_logger.indent++;
}
void logger_dedent(void) {
	if (g_logger.indent > 0)
		g_logger.indent--;
}

ENGINE_API void logger_log_va(LogLevel level, const char *fmt, va_list list) {
}

void logger_log(LogLevel level, const char *file, uint64_t line, const char *fmt, ...) {
	if (level < g_logger.level) {
		return;
	}

	time_t t = time(NULL);
	struct tm *tm_info = localtime(&t);

	char time_buffer[16];
	strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", tm_info);

	va_list arg_ptr;
	va_start(arg_ptr, fmt);

	bool decorate = fmt[0] != '#';

	if (decorate) {
		printf(
			"%s[%-5s]\x1b[0m ", // color, indent, log level
			g_log_level_colors[level], // Start color for the level
			g_level_strings[level] // Log level string
		);
		if (level >= LOG_LEVEL_ERROR)
			printf("%s", g_log_level_colors[level]);
	} else
		fmt += 1;

	vprintf(fmt, arg_ptr);
	if (decorate)
		printf("\x1b[0m");
	printf("\n");
	fflush(stdout);
	va_end(arg_ptr);
}
