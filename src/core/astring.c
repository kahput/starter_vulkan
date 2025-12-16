#include "astring.h"

#include "allocators/arena.h"

#include <stdio.h>
#include <string.h>

bool string_equals(String a, String b) {
	if (a.length != b.length)
		return false;

	return !a.size || !memcmp(a.data, b.data, a.length);
}

bool string_contains(String a, String b) {
	return true;
}

uint64_t string_hash64(String string) {
	uint64_t h = 0x100;

	for (uint32_t index = 0; index < string.length; ++index) {
		h ^= string.data[index] & 255;
		h *= 1111111111111111111;
	}

	return h;
}

String string_copy(struct arena *arena, String target) {
	String copy = target;
	copy.data = arena_push_zero(arena, target.size);

	if (copy.size)
		memcpy(copy.data, target.data, copy.size);
	return copy;
}

String string_copy_length(struct arena *arena, String target) {
	String copy = target;
	copy.data = arena_push_zero(arena, target.length);

	if (copy.length)
		memcpy(copy.data, target.data, copy.length);
	return copy;
}

String string_concat(struct arena *arena, String head, String tail) {
	head = string_copy_length(arena, head);
	head.length += string_copy(arena, tail).length;
	head.size = head.length + 1;
	return head;
}

String string_directory_from_path(Arena *arena, String path) {
	uint32_t final = 0, length = 0;
	uint8_t c;

	while ((c = path.data[length++])) {
		if (c == '/' || c == '\\') {
			final = length - 1;
		}
	}

	String directory = {
		.length = final,
		.size = final + 1,
	};

	directory.data = arena_push_zero(arena, path.size);

	memcpy(directory.data, path.data, directory.size);
	directory.data[directory.length] = '\0';
	return directory;
}

String string_format(Arena *arena, String format, ...) {
	va_list args;
	va_start(args, format);

	// We must copy the args because vsnprintf consumes them.
	// We need to use them twice (once for sizing, once for writing).
	va_list args_copy;
	va_copy(args_copy, args);

	// 1. Pass One: Determine the required length (excluding null terminator)
	// We assume format.data is null-terminated (Safe if using SLITERAL or S macros)
	int length = vsnprintf(NULL, 0, format.data, args);

	if (length < 0) {
		// Handle encoding errors (optional)
		va_end(args_copy);
		va_end(args);
		return (String){ 0 };
	}

	// 2. Allocate exact memory
	// +1 for the null terminator
	size_t size = length + 1;
	char *buffer = arena_push_zero(arena, size);

	// 3. Pass Two: Write the formatted string to the buffer
	vsnprintf(buffer, size, format.data, args_copy);

	va_end(args_copy);
	va_end(args);

	return (String){
		.data = buffer,
		.length = (size_t)length,
		.size = size
	};
}

String string_filename_from_path(Arena *arena, String path) {
	int32_t start = 0;
	uint32_t length = 0;
	uint8_t c;

	while (length < path.length && (c = path.data[length++])) {
		if (c == '/' || c == '\\') {
			start = length;
		}
	}

	String name = {
		.length = length - start,
		.size = (length + 1) - start
	};
	name.data = arena_push_zero(arena, name.size);
	memcpy(name.data, path.data + start, name.length);
	name.data[length] = '\0';

	return name;
}
String string_extension_from_path(struct arena *arena, String name) {
	int32_t start = 0;
	uint32_t length = 0;
	uint8_t c;

	while (length < name.length && (c = name.data[length++])) {
		if (c == '.') {
			start = length;
		}
	}

	String extension = {
		.length = length - start,
		.size = (length + 1) - start
	};
	extension.data = arena_push_zero(arena, extension.size);
	memcpy(extension.data, name.data + start, extension.length);
	extension.data[length] = '\0';

	return extension;
}

size_t cstring_length(const char *string) {
	uint8_t c;
	size_t length = 0;
	while ((c = string[length++])) {
	}

	return length - 1;
}

size_t cstring_nlength(const char *string, size_t max_length) {
	uint8_t c;
	size_t length = 0;
	while (length < max_length && (c = string[length++])) {
	}

	return length - 1;
}

char *cstring_null_terminated(struct arena *arena, String string) {
	if (string.data[string.length] == '\0')
		return (char *)string.data;

	char *null_termianted = arena_push_zero(arena, string.length + 1);
	mempcpy(null_termianted, string.data, string.length + 1);
	null_termianted[string.length] = '\0';

	return null_termianted;
}
