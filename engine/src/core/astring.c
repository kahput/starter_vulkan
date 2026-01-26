#include "astring.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> // For strtod/strtoll logic if needed, though we implement custom mostly

String string_from_cstr(const char *s) {
	if (s == NULL)
		return (String){ 0 };

	return (String){
		.memory = (char *)s,
		.length = strlen(s)
	};
}

String string_from_range(char *start, char *end) {
	if (end < start)
		return (String){ 0 };

	return (String){
		.memory = start,
		.length = (size_t)(end - start)
	};
}

bool string_equals(String a, String b) {
	if (a.length != b.length)
		return false;
	if (a.length == 0)
		return true;
	if (a.memory == b.memory)
		return true;

	return memcmp(a.memory, b.memory, a.length) == 0;
}

bool string_equals_ignore_case(String a, String b) {
	if (a.length != b.length)
		return false;

	for (size_t index = 0; index < a.length; index++) {
		if (tolower(a.memory[index]) != tolower(b.memory[index]))
			return false;
	}

	return true;
}

bool string_has_prefix(String str, String prefix) {
	if (prefix.length > str.length)
		return false;

	return memcmp(str.memory, prefix.memory, prefix.length) == 0;
}

bool string_has_suffix(String str, String suffix) {
	if (suffix.length > str.length)
		return false;

	return memcmp(str.memory + (str.length - suffix.length), suffix.memory, suffix.length) == 0;
}

int64_t string_find_first(String haystack, String needle) {
	if (needle.length == 0)
		return (int64_t)haystack.length;
	if (haystack.length < needle.length)
		return -1;

	for (size_t index = 0; index <= haystack.length - needle.length; index++) {
		if (memcmp(haystack.memory + index, needle.memory, needle.length) == 0) {
			return (int64_t)index;
		}
	}

	return -1;
}

int64_t string_find_last(String haystack, String needle) {
	if (needle.length == 0)
		return (int64_t)haystack.length;
	if (haystack.length < needle.length)
		return -1;

	// Iterate backwards
	for (int64_t index = haystack.length - needle.length; index >= 0; index--) {
		if (memcmp(haystack.memory + index, needle.memory, needle.length) == 0) {
			return index;
		}
	}
	return -1;
}

String string_slice(String str, uint32_t start, uint32_t length) {
	if (start >= str.length)
		return (String){ 0 };
	if (start + length > str.length)
		length = str.length - start;

	return (String){ .memory = str.memory + start, .length = length };
}

String string_chop_left(String str, uint32_t n) {
	if (n >= str.length)
		return (String){ 0 };

	return (String){ .memory = str.memory + n, .length = str.length - n };
}

String string_chop_right(String str, uint32_t n) {
	if (n >= str.length)
		return (String){ 0 };
	return (String){ .memory = str.memory, .length = str.length - n };
}

String string_trim_left(String str) {
	size_t index = 0;
	while (index < str.length && isspace(str.memory[index])) {
		index++;
	}
	return string_chop_left(str, (uint32_t)index);
}

String string_trim_right(String str) {
	size_t i = 0;
	while (i < str.length && isspace(str.memory[str.length - 1 - i])) {
		i++;
	}
	return string_chop_right(str, (uint32_t)i);
}

String string_trim(String s) {
	return string_trim_right(string_trim_left(s));
}

uint64_t string_hash64(String string) {
	uint64_t h = 0x100;

	for (uint32_t index = 0; index < string.length; ++index) {
		h ^= string.memory[index] & 255;
		h *= 1111111111111111111;
	}

	return h;
}

// ==========================================
// Parsing
// ==========================================

int64_t string_to_i64(String str) {
	if (str.length == 0)
		return 0;

	int64_t result = 0;
	int sign = 1;
	size_t index = 0;

	str = string_trim_left(str);
	if (index < str.length && (str.memory[index] == '+' || str.memory[index] == '-')) {
		sign = (str.memory[index] == '-') ? -1 : 1;
		index++;
	}

	while (index < str.length && isdigit(str.memory[index])) {
		result = result * 10 + (str.memory[index] - '0');
		index++;
	}

	return result * sign;
}

double string_to_f64(String str) {
	char buffer[128];
	if (str.length >= sizeof(buffer))
		return 0.0;

	memcpy(buffer, str.memory, str.length);
	buffer[str.length] = '\0';

	return strtod(buffer, NULL);
}

static bool is_path_separator(char c) {
	return c == '/' || c == '\\';
}

String string_path_folder(String path) {
	if (path.length == 0)
		return (String){ 0 };

	for (int64_t index = path.length - 1; index >= 0; index--) {
		if (is_path_separator(path.memory[index])) {
			return string_slice(path, 0, (uint32_t)index);
		}
	}

	return (String){ 0 };
}

String string_path_filename(String path) {
	if (path.length == 0)
		return (String){ 0 };

	for (int64_t index = path.length - 1; index >= 0; index--) {
		if (is_path_separator(path.memory[index])) {
			return string_slice(path, (uint32_t)(index + 1), (uint32_t)(path.length - index - 1));
		}
	}
	return path;
}

String string_path_extension(String path) {
	if (path.length == 0)
		return (String){ 0 };

	// Scan backwards, but stop if we hit a path separator
	for (int64_t index = path.length - 1; index >= 0; index--) {
		if (is_path_separator(path.memory[index]))
			break;
		if (path.memory[index] == '.') {
			return string_slice(path, (uint32_t)(index + 1), (uint32_t)(path.length - index - 1));
		}
	}

	return (String){ 0 };
}

String string_push_copy(Arena *arena, String str) {
	if (str.length == 0)
		return (String){ 0 };

	// Allocate length + 1 for implicit null termination convenience
	char *data = arena_push_array(arena, char, str.length + 1);
	memcpy(data, str.memory, str.length);
	data[str.length] = '\0';

	return (String){ .memory = data, .length = str.length };
}

String string_pushfv(Arena *arena, const char *format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);

	// Pass 1: Measure
	int length = vsnprintf(NULL, 0, format, args);
	if (length < 0) {
		va_end(args_copy);
		return (String){ 0 };
	}

	// Pass 2: Write
	char *data = arena_push_array(arena, char, length + 1);
	vsnprintf(data, length + 1, format, args_copy);
	va_end(args_copy);

	return (String){ .memory = data, .length = (size_t)length };
}

String string_pushf(Arena *arena, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	String result = string_pushfv(arena, fmt, args);
	va_end(args);
	return result;
}

String string_push_concat(Arena *arena, String head, String tail) {
	size_t new_length = head.length + tail.length;
	char *data = arena_push_array(arena, char, new_length + 1);

	memcpy(data, head.memory, head.length);
	memcpy(data + head.length, tail.memory, tail.length);
	data[new_length] = '\0';

	return (String){ .memory = data, .length = new_length };
}

String string_push_replace(Arena *arena, String source, String find, String replace) {
	if (find.length == 0)
		return string_push_copy(arena, source);

	size_t count = 0;
	size_t offset = 0;
	while (offset < source.length) {
		int64_t index = string_find_first(
			(String){ .memory = source.memory + offset, .length = source.length - offset },
			find);
		if (index == -1)
			break;

		count++;
		offset = index + find.length;
	}

	size_t new_length = source.length + count * (replace.length - find.length);
	char *data = arena_push_array(arena, char, new_length + 1);

	// Pass 2: Build string
	size_t src_offset = 0;
	size_t dst_offset = 0;
	while (src_offset < source.length) {
		int64_t index = string_find_first(
			(String){ .memory = source.memory + src_offset, .length = source.length - src_offset },
			find);

		if (index == -1) {
			// Copy remaining
			size_t remain = source.length - src_offset;
			memcpy(data + dst_offset, source.memory + src_offset, remain);
			dst_offset += remain;
			break;
		}

		// Copy segment before match
		size_t seg_len = index - src_offset;
		memcpy(data + dst_offset, source.memory + src_offset, seg_len);
		dst_offset += seg_len;
		src_offset += seg_len;

		// Copy replacement
		memcpy(data + dst_offset, replace.memory, replace.length);
		dst_offset += replace.length;
		src_offset += find.length;
	}

	data[new_length] = '\0';
	return (String){ .memory = data, .length = new_length };
}

String string_push_upper(Arena *arena, String s) {
	String copy = string_push_copy(arena, s);
	for (size_t index = 0; index < copy.length; index++)
		copy.memory[index] = toupper(copy.memory[index]);

	return copy;
}

String string_push_lower(Arena *arena, String s) {
	String copy = string_push_copy(arena, s);
	for (size_t index = 0; index < copy.length; index++)
		copy.memory[index] = tolower(copy.memory[index]);

	return copy;
}
String string_push_path_join(Arena *arena, String head, String tail) {
	if (head.length == 0)
		return string_push_copy(arena, tail);
	if (tail.length == 0)
		return string_push_copy(arena, head);

	bool head_has_sep = is_path_separator(head.memory[head.length - 1]);
	bool tail_has_sep = is_path_separator(tail.memory[0]);

	if (head_has_sep && tail_has_sep) {
		// "folder/" + "/file" -> "folder//file" (need to remove one)
		return string_push_concat(arena, head, string_chop_left(tail, 1));
	} else if (!head_has_sep && !tail_has_sep) {
		// "folder" + "file" -> "folder/file" (need to add one)
		// Manual construction to avoid double allocation of concat(concat)
		size_t new_length = head.length + 1 + tail.length;
		char *data = arena_push_array(arena, char, new_length + 1);
		memcpy(data, head.memory, head.length);
		data[head.length] = '/';
		memcpy(data + head.length + 1, tail.memory, tail.length);
		data[new_length] = '\0';
		return (String){ .memory = data, .length = new_length };
	} else
		// "folder/" + "file" OR "folder" + "/file" -> Clean concat
		return string_push_concat(arena, head, tail);
}

char *string_push_cstring(Arena *arena, String s) {
	char *cstr = arena_push_array(arena, char, s.length + 1);
	memcpy(cstr, s.memory, s.length);
	cstr[s.length] = '\0';
	return cstr;
}

void string_list_push(Arena *arena, StringList *list, String s) {
	StringNode *node = arena_push_struct(arena, StringNode);
	node->string = s;
	node->next = NULL;

	if (list->first == NULL) {
		list->first = node;
		list->last = node;
	} else {
		list->last->next = node;
		list->last = node;
	}

	list->node_count++;
	list->total_length += s.length;
}

StringList string_list_split(Arena *arena, String str, String separator) {
	StringList list = { 0 };
	uint32_t cursor = 0;

	while (cursor < str.length) {
		int64_t index = string_find_first(
			(String){ .memory = str.memory + cursor, str.length - cursor },
			separator);

		if (index == -1) {
			// Push remainder
			string_list_push(arena, &list, string_slice(str, cursor, str.length - cursor));
			break;
		}

		string_list_push(arena, &list, string_slice(str, cursor, (uint32_t)index - cursor));
		cursor = (uint32_t)index + (uint32_t)separator.length;
	}

	// Edge case: if string ends with separator, do we push empty string?
	// Standard split behavior usually says yes.
	if (cursor == str.length && str.length > 0)
		string_list_push(arena, &list, (String){ 0 });

	return list;
}
String string_list_join(Arena *arena, StringList *list, String separator) {
	if (list->node_count == 0)
		return (String){ 0 };

	// Calculate exact size needed
	size_t total_length = list->total_length + (list->node_count - 1) * separator.length;

	char *data = arena_push_array(arena, char, total_length + 1);
	size_t cursor = 0;

	StringNode *node = list->first;
	while (node) {
		memcpy(data + cursor, node->string.memory, node->string.length);
		cursor += node->string.length;

		if (node->next) {
			memcpy(data + cursor, separator.memory, separator.length);
			cursor += separator.length;
		}

		node = node->next;
	}

	data[total_length] = '\0';
	return (String){ .memory = data, .length = total_length };
}
