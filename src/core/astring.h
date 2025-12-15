#pragma once

#include "common.h"
#include "string.h"

struct arena;

typedef struct {
	size_t length, size;
	uint8_t *data;
} String;

#define SLITERAL(s) \
	(String) { .data = (uint8_t *)s, .length = sizeof(s) - 1, .size = sizeof(s) }
#define S(s) \
	(String){ .data = (uint8_t *)s, .length = cstring_length(s), .size = cstring_length(s) + 1 }

bool string_equals(String a, String b);
bool string_contains(String a, String b);

uint64_t string_hash64(String string);
String string_copy(struct arena *arena, String target);
String string_copy_content(struct arena *arena, String target);
String string_concat(struct arena *arena, String head, String tail);

String string_directory_from_path(struct arena *arena, String path);
String string_filename_from_path(struct arena *arena, String path);
String string_extension_from_path(struct arena *arena, String name);

size_t cstring_length(const char *);
size_t cstring_nlength(const char *, size_t);
