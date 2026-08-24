#include "strings.h"
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

bool str8__ispathdelim(char c) {
	return c == '/' || c == '\\';
}

String8 str8_wrap(const char *cstring) {
	return (String8){ .text = (uint8_t *)cstring, .length = strlen(cstring) };
}

bool str8_equals(String8 a, String8 b) {
	if (a.length != b.length)
		return false;

	return memory_compare(a.text, b.text, a.length) == 0;
}

bool str8_contains(String8 haystack, String8 needle) {
	if (haystack.length < needle.length) return false;

	for (uint64_t index = 0; index <= haystack.length - needle.length; ++index) 
		if (str8_equals((String8){ haystack.text + index, needle.length }, needle))
			return true;
	

	return false;
}

String8 str8_concat(Arena *arena, String8 a, String8 b) {
	if (a.length == 0)
		return b;
	if (b.length == 0)
		return a;

	String8 result = { 0 };

	uint32_t size = a.length + b.length + 1;
	result.length = size - 1;

	result.text = arena_push(arena, size, 1, false);
	memory_copy(result.text, a.text, a.length);
	memory_copy(result.text + a.length, b.text, b.length);

	result.text[result.length] = '\0';

	return result;
}

String8 str8_indent(Arena *arena, String8 indent, uint32_t depth) {
	String8 result = { 0 };

	bool ok = arena && indent.length;
	if (ok) {
		result.text = (uint8_t *)arena->base + arena->offset;
		result.length = arena->offset;

		for (uint32_t index = 0; index < depth; ++index)
			str8_pushf(arena, s("%.*s"), str_spread(indent));

		result.length = arena->offset - result.length;
	}

	return result;
}

String8 str8_filepath_join(Arena *arena, String8 head, String8 tail) {
	if (head.length == 0)
		return tail;
	if (tail.length == 0)
		return head;

	String8 result = { 0 };

	if (str8__ispathdelim(head.text[head.length - 1]) && str8__ispathdelim(tail.text[0])) {
		head.length -= 1;
		result = str8_concat(arena, head, tail);
	} else if (str8__ispathdelim(head.text[head.length - 1]) || str8__ispathdelim(tail.text[0])) {
		result = str8_concat(arena, head, tail);
	} else {
		result.length = head.length + tail.length + 1; // + path delimiter
		result.text = arena_push(arena, result.length + 1, 1, false); // + null terminator
		memory_copy(result.text, head.text, head.length);
		result.text[head.length] = '/';
		memory_copy(result.text + head.length + 1, tail.text, tail.length);
		result.text[result.length] = '\0';
	}

	return result;
}

String8 str8_copy(Arena *arena, String8 src) {
	String8 result = { 0 };

	bool ok = arena && src.length;
	if (ok) {
		result.length = src.length;
		uint32_t new_size = src.text[src.length - 1] != '\0' ? src.length + 1 : src.length;

		result.text = arena_push_count(arena, uint8_t, new_size);
		memory_copy(result.text, src.text, src.length);
		result.text[new_size] = '\0';
	}

	return result;
}

String8 str8_push_format_list(Arena *arena, String8 format, va_list args) {
	String8 result = { 0 };

	bool ok = arena;
	if (ok == false)
		LOG_WARN("%s - invalid parameters", __func__);

	int32_t length = 0;
	if (ok) {
		ASSERT(format.text[format.length] == '\0');

		va_list copy;
		va_copy(copy, args);

		length = vsnprintf(0, 0, (char *)format.text, copy);
		ok = length >= 0;
		va_end(copy);
	}

	if (ok) {
		result.length = length;
		result.text = arena_push_count(arena, uint8_t, result.length + 1);
		vsnprintf((char *)result.text, result.length + 1, (char *)format.text, args);
	}

	return result;
}
String8 str8_pushf(Arena *arena, String8 format, ...) {
	va_list args;
	va_start(args, format);
	String8 result = str8_push_format_list(arena, format, args);
	va_end(args);

	return result;
}

String8 str8_filename(String8 path) {
	if (path.length == 0)
		return path;

	String8 result = path;

	for (uint32_t index = 0; index < path.length; ++index) {
		if (str8__ispathdelim(path.text[index])) {
			result.text = path.text + index + 1;
			result.length = path.length - (index + 1);
		}
	}

	return result;
}

String8 str8_directory(String8 path) {
	if (path.length == 0)
		return path;

	String8 result = path;
	for (int32_t index = path.length - 1; index >= 0; --index) {
		if (str8__ispathdelim(path.text[index])) {
			result.length = index;
			break;
		}
	}

	return result;
}

double str8_to_f64(String8 s) {
	return strtod((char *)s.text, 0);
}

uint64_t str8_to_u64(String8 s) {
	return strtoull((char *)s.text, 0, 10);
}

int64_t str8_to_s64(String8 s) {
	return strtol((char *)s.text, 0, 10);
}
