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

#define STRING_START 0
#define STRING_END UINT32_MAX

String string_create(Arena *arena, size_t size);

bool string_equals(String a, String b);
// Returns substring start index if true, else -1
int32_t string_contains(String a, String b);

uint64_t string_hash64(String string);
String string_duplicate(Arena *arena, String target);
String string_duplicate_by_length(Arena *arena, String target);
String string_slice(Arena *arena, String a, uint32_t start, uint32_t length);
String string_concat(Arena *arena, String head, String tail);

String string_insert_at(Arena *arena, String into, String insert, uint32_t index);
String string_find_and_replace(Arena *arena, String string, String find, String replace);

String string_format(Arena *arena, String format, ...);

String string_directory_from_path(Arena *arena, String path);
String string_filename_from_path(Arena *arena, String path);
String string_extension_from_path(Arena *arena, String name);

size_t cstring_length(const char *);
size_t cstring_nlength(const char *, size_t);

char *cstring_null_terminated(Arena *arena, String string);
