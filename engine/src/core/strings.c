#include "strings.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

bool str8__ispathdelim(char c) {
	return c == '/' || c == '\\';
}

bool has8(string8 haystack, string8 needle) {
	if (haystack.length < needle.length) return false;

	for (uint64_t index = 0; index <= haystack.length - needle.length; ++index)
		if (eq8(str8(haystack.bytes + index, needle.length), needle))
			return true;

	return false;
}

string8 concat8(Arena *arena, string8 a, string8 b) {
	if (a.length == 0)
		return b;
	if (b.length == 0)
		return a;

	string8 result = { 0 };

	uint32_t size = a.length + b.length + 1;
	result.length = size - 1;

	result.bytes = arena_push(arena, size, 1, false);
	memory_copy(result.bytes, a.bytes, a.length);
	memory_copy(result.bytes + a.length, b.bytes, b.length);

	result.bytes[result.length] = '\0';

	return result;
}

string8 indent8(Arena *arena, string8 indent, uint32_t depth) {
	string8 result = { 0 };

	bool ok = arena && indent.length;
	if (ok) {
		result.bytes = (uint8_t *)arena->base + arena->offset;
		result.length = indent.length * depth;

		for (uint32_t index = 0; index < depth; ++index)
			arena_push_copy(arena, indent.bytes, indent.length, 0);
	}

	return result;
}

string8 upper8(Arena *arena, string8 s) {
	string8 result = copy8(arena, s);

	for (uint32_t index = 0; index < result.length; ++index)
		result.bytes[index] = toupper(result.bytes[index]);

	return result;
}

string8 lower8(Arena *arena, string8 s) {
	string8 result = copy8(arena, s);

	for (uint32_t index = 0; index < result.length; ++index)
		result.bytes[index] = tolower(result.bytes[index]);

	return result;
}

string8 dedent8(Arena *arena, string8 str) {
	string8 result = { 0 };
	uint64_t min_indent = UINT64_MAX;

	bool ok = str.length;
	if (ok) {
		min_indent = UINT64_MAX;
		uint64_t cur_indent = 0;
		bool is_line_start = true;

		for (uint64_t i = 0; i < str.length; ++i) {
			uint8_t c = str.bytes[i];

			if (is_line_start) {
				if (c == ' ' || c == '\t') {
					cur_indent++;
				} else if (c == '\r' || c == '\n') {
					cur_indent = 0; // Blank line; ignore for min_indent
				} else {
					if (cur_indent < min_indent) {
						min_indent = cur_indent;
					}
					is_line_start = false;
				}
			}

			if (c == '\n') {
				is_line_start = true;
				cur_indent = 0;
			}
		}

		ok = min_indent != UINT64_MAX && min_indent != 0;
	}

	if (ok) {
		result.bytes = arena_push_count(arena, uint8_t, str.length);
		result.length = 0;
		uint64_t skipped = 0;
		bool is_line_start = true;

		for (uint64_t i = 0; i < str.length; ++i) {
			uint8_t c = str.bytes[i];

			if (is_line_start && skipped < min_indent && (c == ' ' || c == '\t')) {
				skipped++;
				continue;
			}

			is_line_start = false;
			result.bytes[result.length++] = c;

			if (c == '\n') {
				is_line_start = true;
				skipped = 0;
			}
		}
	}

	return result;
}

string8 pathjoin8(Arena *arena, string8 head, string8 tail) {
	if (head.length == 0)
		return tail;
	if (tail.length == 0)
		return head;

	string8 result = { 0 };

	if (str8__ispathdelim(head.bytes[head.length - 1]) && str8__ispathdelim(tail.bytes[0])) {
		head.length -= 1;
		result = concat8(arena, head, tail);
	} else if (str8__ispathdelim(head.bytes[head.length - 1]) || str8__ispathdelim(tail.bytes[0])) {
		result = concat8(arena, head, tail);
	} else {
		result.length = head.length + tail.length + 1; // + path delimiter
		result.bytes = arena_push(arena, result.length + 1, 1, false); // + null terminator
		memory_copy(result.bytes, head.bytes, head.length);
		result.bytes[head.length] = '/';
		memory_copy(result.bytes + head.length + 1, tail.bytes, tail.length);
		result.bytes[result.length] = '\0';
	}

	return result;
}

string8 copy8(Arena *arena, string8 src) {
	string8 result = { 0 };

	bool ok = arena && src.length;
	if (ok) {
		result.length = src.length;
		result.bytes = arena_push_count(arena, uint8_t, result.length + 1);

		memory_copy(result.bytes, src.bytes, src.length);
		result.bytes[src.length] = '\0';
	}

	return result;
}

string8 fmtv8(Arena *arena, const char *fmt, va_list args) {
	string8 result = { 0 };

	bool ok = arena;
	if (ok == false)
		LOG_WARN("%s - invalid parameters", __func__);

	int32_t length = 0;
	if (ok) {
		va_list copy;
		va_copy(copy, args);

		length = vsnprintf(0, 0, (char *)fmt, copy);
		ok = length >= 0;
		va_end(copy);
	}

	if (ok) {
		result.length = length;
		result.bytes = arena_push_count(arena, uint8_t, result.length + 1);
		vsnprintf((char *)result.bytes, result.length + 1, (char *)fmt, args);
	}

	return result;
}
string8 fmt8(Arena *arena, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	string8 result = fmtv8(arena, fmt, args);
	va_end(args);

	return result;
}

string8 pathfile8(string8 path) {
	if (path.length == 0)
		return path;

	string8 result = path;

	for (uint32_t index = 0; index < path.length; ++index) {
		if (str8__ispathdelim(path.bytes[index])) {
			result.bytes = path.bytes + index + 1;
			result.length = path.length - (index + 1);
		}
	}

	return result;
}

string8 pathext8(string8 file) {
	string8 result = { 0 };

	bool ok = file.length;
	if (ok) {
		int32_t dot = -1;
		for (int32_t index = file.length - 1; index >= 0; --index) {
			char c = file.bytes[index];

			if (c == '.') {
				dot = index;
				break;
			}
		}

		if (dot != -1)
			result = (string8){ file.bytes + dot + 1, file.length - (dot + 1) };
	}

	return result;
}

string8 pathdir8(string8 path) {
	if (path.length == 0)
		return path;

	string8 result = path;
	for (int32_t index = path.length - 1; index >= 0; --index) {
		if (str8__ispathdelim(path.bytes[index])) {
			result.length = index;
			break;
		}
	}

	return result;
}

double str8_to_f64(string8 s) {
	return strtod((char *)s.bytes, 0);
}

uint64_t str8_to_u64(string8 s) {
	return strtoull((char *)s.bytes, 0, 10);
}

int64_t str8_to_s64(string8 s) {
	return strtol((char *)s.bytes, 0, 10);
}
