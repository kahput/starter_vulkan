#include "strings.h"
#include "common.h"
#include "core/arena.h"

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

String8 str8_concat(Arena *arena, String8 a, String8 b) {
	if (a.length == 0)
		return b;
	if (b.length == 0)
		return a;

	String8 result = { 0 };

	result.length = a.length + b.length + 1;
	result.text = arena_push(arena, result.length, 1, false);
	memory_copy(result.text, a.text, a.length);
	memory_copy(result.text + a.length, b.text, b.length);
	result.text[result.length] = '\0';

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
