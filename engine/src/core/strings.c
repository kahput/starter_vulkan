#include "strings.h"

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h> // For strtod/strtoll logic if needed, though we implement custom mostly

String string_wrap(const char *s) {
	if (s == NULL)
		return (String){ 0 };

	return (String){
		.chars = (char *)s,
		.length = strlen(s)
	};
}

bool string_equals(String a, String b) {
	if (a.length != b.length)
		return false;
	if (a.length == 0)
		return true;
	if (a.chars == b.chars)
		return true;

	return memcmp(a.chars, b.chars, a.length) == 0;
}

bool string_equals_ignore_case(String a, String b) {
	if (a.length != b.length)
		return false;

	for (size_t index = 0; index < a.length; index++) {
		if (tolower(a.chars[index]) != tolower(b.chars[index]))
			return false;
	}

	return true;
}

bool string_has_prefix(String str, String prefix) {
	if (prefix.length > str.length)
		return false;

	return memcmp(str.chars, prefix.chars, prefix.length) == 0;
}

bool string_has_suffix(String str, String suffix) {
	if (suffix.length > str.length)
		return false;

	return memcmp(str.chars + (str.length - suffix.length), suffix.chars, suffix.length) == 0;
}

int64_t string_find_first(String haystack, String needle) {
	if (needle.length == 0)
		return (int64_t)haystack.length;
	if (haystack.length < needle.length)
		return -1;

	for (size_t index = 0; index <= haystack.length - needle.length; index++) {
		if (memcmp(haystack.chars + index, needle.chars, needle.length) == 0) {
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
		if (memcmp(haystack.chars + index, needle.chars, needle.length) == 0) {
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

	return (String){ .chars = str.chars + start, .length = length };
}

String string_chop_left(String str, uint32_t n) {
	if (n >= str.length)
		return (String){ 0 };

	return (String){ .chars = str.chars + n, .length = str.length - n };
}

String string_chop_right(String str, uint32_t n) {
	if (n >= str.length)
		return (String){ 0 };
	return (String){ .chars = str.chars, .length = str.length - n };
}

String string_trim_left(String str) {
	size_t index = 0;
	while (index < str.length && isspace(str.chars[index])) {
		index++;
	}
	return string_chop_left(str, (uint32_t)index);
}

String string_trim_right(String str) {
	size_t i = 0;
	while (i < str.length && isspace(str.chars[str.length - 1 - i])) {
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
		h ^= string.chars[index] & 255;
		h *= 1111111111111111111;
	}

	return h;
}

// ==========================================
// Parsing
// ==========================================

uint32_t string_to_u32(String str) {
	if (str.length == 0)
		return 0;

	uint32_t result = 0;
	size_t index = 0;

	while (index < str.length && isdigit(str.chars[index])) {
		result = result * 10 + (str.chars[index] - '0');
		index++;
	}

	return result;
}
uint64_t string_to_u64(String str) {
	if (str.length == 0)
		return 0;

	uint64_t result = 0;
	size_t index = 0;

	while (index < str.length && isdigit(str.chars[index])) {
		result = result * 10 + (str.chars[index] - '0');
		index++;
	}

	return result;
}

int32_t string_to_i32(String str) {
	if (str.length == 0)
		return 0;

	int32_t result = 0;
	int sign = 1;
	size_t index = 0;

	str = string_trim_left(str);
	if (index < str.length && (str.chars[index] == '+' || str.chars[index] == '-')) {
		sign = (str.chars[index] == '-') ? -1 : 1;
		index++;
	}

	while (index < str.length && isdigit(str.chars[index])) {
		result = result * 10 + (str.chars[index] - '0');
		index++;
	}

	return result * sign;
}

int64_t string_to_i64(String str) {
	if (str.length == 0)
		return 0;

	int64_t result = 0;
	int sign = 1;
	size_t index = 0;

	str = string_trim_left(str);
	if (index < str.length && (str.chars[index] == '+' || str.chars[index] == '-')) {
		sign = (str.chars[index] == '-') ? -1 : 1;
		index++;
	}

	while (index < str.length && isdigit(str.chars[index])) {
		result = result * 10 + (str.chars[index] - '0');
		index++;
	}

	return result * sign;
}

float string_to_f32(String str) {
	char buffer[128];
	if (str.length >= sizeof(buffer))
		return 0.0f;

	memory_copy(buffer, str.chars, str.length);
	buffer[str.length] = '\0';

	return strtof(buffer, NULL);
}
double string_to_f64(String str) {
	char buffer[128];
	if (str.length >= sizeof(buffer))
		return 0.0;

	memory_copy(buffer, str.chars, str.length);
	buffer[str.length] = '\0';

	return strtod(buffer, NULL);
}

static bool is_path_separator(char c) {
	return c == '/' || c == '\\';
}

String stringpath_directory(String path) {
	if (path.length == 0)
		return (String){ 0 };

	for (int64_t index = path.length - 1; index >= 0; index--) {
		if (is_path_separator(path.chars[index])) {
			return string_slice(path, 0, (uint32_t)index);
		}
	}

	return (String){ 0 };
}

String stringpath_filename(String path) {
	if (path.length == 0)
		return (String){ 0 };

	for (int64_t index = path.length - 1; index >= 0; index--) {
		if (is_path_separator(path.chars[index])) {
			return string_slice(path, (uint32_t)(index + 1), (uint32_t)(path.length - index - 1));
		}
	}
	return path;
}

String stringpath_extension(String path) {
	if (path.length == 0)
		return (String){ 0 };

	// Scan backwards, but stop if we hit a path separator
	for (int64_t index = path.length - 1; index >= 0; index--) {
		if (is_path_separator(path.chars[index]))
			break;
		if (path.chars[index] == '.') {
			return string_slice(path, (uint32_t)(index + 1), (uint32_t)(path.length - index - 1));
		}
	}

	return (String){ 0 };
}
String stringpath_join(Arena *arena, String head, String tail) {
	if (head.length == 0)
		return string_copy(arena, tail);
	if (tail.length == 0)
		return string_copy(arena, head);

	bool head_has_sep = is_path_separator(head.chars[head.length - 1]);
	bool tail_has_sep = is_path_separator(tail.chars[0]);

	if (head_has_sep && tail_has_sep)
		return string_concat(arena, head, string_chop_left(tail, 1));
	else if (!head_has_sep && !tail_has_sep) {
		// "folder" + "file" -> "folder/file" (need to add one)
		// Manual construction to avoid double allocation of concat(concat)
		size_t new_length = head.length + 1 + tail.length;
		char *data = arena_push_count(arena, new_length + 1, char);
		memory_copy(data, head.chars, head.length);
		data[head.length] = '/';
		memory_copy(data + head.length + 1, tail.chars, tail.length);
		data[new_length] = '\0';
		return (String){ .chars = data, .length = new_length };
	} else
		// "folder/" + "file" OR "folder" + "/file" -> Clean concat
		return string_concat(arena, head, tail);
}

String stringpath_normalize(Arena *arena, String path) {
	String result = string_wrap(arena_push_count(arena, path.length, char));
	for (uint32_t index = 0; index < path.length; ++index) {
		if (path.chars[index] == '\\')
			continue;

		result.chars[result.length++] = path.chars[index];
	}

	return result;
}

String string_copy(Arena *arena, String str) {
	if (str.length == 0)
		return (String){ 0 };

	// Allocate length + 1 for implicit null termination convenience
	char *data = arena_push_count(arena, str.length + 1, char);
	memory_copy(data, str.chars, str.length);
	data[str.length] = '\0';

	return (String){ .chars = data, .length = str.length };
}

String string_formatv(Arena *arena, const char *format, va_list args) {
	va_list args_copy;
	va_copy(args_copy, args);

	// Pass 1: Measure
	int length = vsnprintf(NULL, 0, format, args);
	if (length < 0) {
		va_end(args_copy);
		return (String){ 0 };
	}

	// Pass 2: Write
	char *data = arena_push_count(arena, length + 1, char);
	vsnprintf(data, length + 1, format, args_copy);
	va_end(args_copy);

	return (String){ .chars = data, .length = (size_t)length };
}

String string_format(Arena *arena, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	String result = string_formatv(arena, fmt, args);
	va_end(args);
	return result;
}

String string_concat(Arena *arena, String head, String tail) {
	size_t new_length = head.length + tail.length;
	char *data = arena_push_count(arena, new_length + 1, char);

	memory_copy(data, head.chars, head.length);
	memory_copy(data + head.length, tail.chars, tail.length);
	data[new_length] = '\0';

	return (String){ .chars = data, .length = new_length };
}

String string_replace(Arena *arena, String source, String find, String replace) {
	if (find.length == 0)
		return string_copy(arena, source);

	size_t count = 0;
	size_t offset = 0;
	while (offset < source.length) {
		int64_t index = string_find_first(
			(String){ .chars = source.chars + offset, .length = source.length - offset },
			find);
		if (index == -1)
			break;

		count++;
		offset = index + find.length;
	}

	size_t new_length = source.length + count * (replace.length - find.length);
	char *data = arena_push_count(arena, new_length + 1, char);

	// Pass 2: Build string
	size_t src_offset = 0;
	size_t dst_offset = 0;
	while (src_offset < source.length) {
		int64_t index = string_find_first(
			(String){ .chars = source.chars + src_offset, .length = source.length - src_offset },
			find);

		if (index == -1) {
			// Copy remaining
			size_t remain = source.length - src_offset;
			memory_copy(data + dst_offset, source.chars + src_offset, remain);
			dst_offset += remain;
			break;
		}

		// Copy segment before match
		size_t seg_len = index - src_offset;
		memory_copy(data + dst_offset, source.chars + src_offset, seg_len);
		dst_offset += seg_len;
		src_offset += seg_len;

		// Copy replacement
		memory_copy(data + dst_offset, replace.chars, replace.length);
		dst_offset += replace.length;
		src_offset += find.length;
	}

	data[new_length] = '\0';
	return (String){ .chars = data, .length = new_length };
}

String string_upper(Arena *arena, String s) {
	String copy = string_copy(arena, s);
	for (size_t index = 0; index < copy.length; index++)
		copy.chars[index] = toupper(copy.chars[index]);

	return copy;
}

String string_lower(Arena *arena, String s) {
	String copy = string_copy(arena, s);
	for (size_t index = 0; index < copy.length; index++)
		copy.chars[index] = tolower(copy.chars[index]);

	return copy;
}

char *string_cstring(Arena *arena, String s) {
	char *cstr = arena_push_count(arena, s.length + 1, char);
	memory_copy(cstr, s.chars, s.length);
	cstr[s.length] = '\0';
	return cstr;
}

void stringlist_push(Arena *arena, StringList *list, String s) {
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

	list->count++;
	list->total_length += s.length;
}

StringList stringlist_split(Arena *arena, String str, String separator) {
	StringList list = { 0 };
	uint32_t cursor = 0;

	while (cursor < str.length) {
		int64_t index = string_find_first(
			(String){ .chars = str.chars + cursor, str.length - cursor },
			separator);

		if (index == -1) {
			// Push remainder
			stringlist_push(arena, &list, string_slice(str, cursor, str.length - cursor));
			break;
		}

		stringlist_push(arena, &list, string_slice(str, cursor, (uint32_t)index - cursor));
		cursor = (uint32_t)index + (uint32_t)separator.length;
	}

	// Edge case: if string ends with separator, do we push empty string?
	// Standard split behavior usually says yes.
	if (cursor == str.length && str.length > 0)
		stringlist_push(arena, &list, (String){ 0 });

	return list;
}
String stringlist_join(Arena *arena, StringList *list, String separator) {
	if (list->count == 0)
		return (String){ 0 };

	// Calculate exact size needed
	size_t total_length = list->total_length + (list->count - 1) * separator.length;

	char *data = arena_push_count(arena, total_length + 1, char);
	size_t cursor = 0;

	StringNode *node = list->first;
	while (node) {
		memory_copy(data + cursor, node->string.chars, node->string.length);
		cursor += node->string.length;

		if (node->next) {
			memory_copy(data + cursor, separator.chars, separator.length);
			cursor += separator.length;
		}

		node = node->next;
	}

	data[total_length] = '\0';
	return (String){ .chars = data, .length = total_length };
}
