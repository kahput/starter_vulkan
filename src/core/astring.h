#pragma once

#include "common.h"

#include "allocators/arena.h"
#include "string.h"

struct arena;

typedef struct {
	size_t length, size;
	char *data;
} String;

#define SLITERAL(s) \
	(String) { .data = (char *)s, .length = sizeof(s) - 1, .size = sizeof(s) }
#define S(s) \
	(String){ .data = (char *)s, .length = cstring_length(s), .size = cstring_length(s) + 1 }

bool string_equals(String a, String b);
bool string_contains(String a, String b);

uint64_t string_hash64(String string);
String string_copy(Arena *arena, String target);
String string_copy_content(Arena *arena, String target);
String string_concat(Arena *arena, String head, String tail);

String string_format(Arena *arena, String format, ...);

String string_directory_from_path(Arena *arena, String path);
String string_filename_from_path(Arena *arena, String path);
String string_extension_from_path(Arena *arena, String name);

size_t cstring_length(const char *);
size_t cstring_nlength(const char *, size_t);

char *cstring_null_terminated(Arena *arena, String string);
