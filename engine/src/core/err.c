#include "err.h"
#include "core/strings.h"
#include <stdio.h>

bool _err_report(const char *func, const char *file, int line, const char *cond_str, const char *msg) {
	printf("%s:%d: error [%s()] %s\n%6d | %s\n", file, line, func, msg, line, cond_str);

	return false;
}

bool _err_report_index(const char *func, const char *file, int line, uint64_t index, uint64_t bound, const char *index_str, const char *bound_str, const char *msg) {
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s = %lu, %s = %lu", index_str, index, bound_str, bound);
	_err_report(func, file, line, buffer, msg);

	return false;
}
