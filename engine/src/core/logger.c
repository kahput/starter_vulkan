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

static const char *basename(const char *path) {
	const char *last_slash = strrchr(path, '/');
	const char *last_backslash = strrchr(path, '\\');
	const char *basename = path;

	if (last_slash && last_backslash) {
		basename = (last_slash > last_backslash) ? last_slash + 1 : last_backslash + 1;
	} else if (last_slash) {
		basename = last_slash + 1;
	} else if (last_backslash) {
		basename = last_backslash + 1;
	}

	return basename;
}

void logger_indent(void) {
	g_logger.indent++;
}
void logger_dedent(void) {
	if (g_logger.indent > 0)
		g_logger.indent--;
}

ENGINE_API void logger_log(LogLevel level, const char *file, int line, const char *format, ...) {
	if (level < g_logger.level) {
		return;
	}

	time_t t = time(NULL);
	struct tm *tm_info = localtime(&t);

	char time_buffer[16];
	strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", tm_info);

	char indent_buffer[32];
	memset(indent_buffer, ' ', sizeof(indent_buffer));
	int32_t indent_space = min(g_logger.indent, 15) * 2;
	indent_buffer[indent_space] = '\0';

	va_list arg_ptr;
	va_start(arg_ptr, format);
	printf(
		"%s %s%s[%s]\x1b[0m \x1b[37m%s:%d:\x1b[0m ",
		time_buffer, // Timestamp
		g_log_level_colors[level], // Start color for the level
		indent_buffer,
		g_level_strings[level], // Log level string
		basename(file), // Source file name
		line // Line number in source file
	);
	if (level >= LOG_LEVEL_ERROR)
		printf("%s", g_log_level_colors[level]);
	vprintf(format, arg_ptr);
	printf("\x1b[0m\n");
	fflush(stdout);
	va_end(arg_ptr);
}
