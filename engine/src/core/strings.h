#pragma once

#include "common.h"
#include "core/arena.h"

typedef struct {
	uint8_t *text;
	uint64_t length;
} String8;

#define s(s) slit(s)
#define sarg(s) s.length, s.text
#define slit(s) (String8){ .text = (uint8_t *)s, .length = sizeof(s) - 1 }
#define scomp(s) { .text = (uint8_t *)s, .length = sizeof(s) - 1 }
#define sspread(s) (int32_t)s.length, (char *)s.text
#define shash(s) hash64((s), sizeof((s)) - 1)


String8 str8_wrap(const char *cstring);
INLINE String8 str8_from_ends(const char *start, const char *end) {
	if (end <= start) return (String8){ 0 };
	return (String8){ .text = (uint8_t *)start, .length = end - start };
}


bool str8_equals(String8 a, String8 b);
bool str8_contains(String8 haystack, String8 needle);

String8 str8_concat(Arena *arena, String8 a, String8 b);
String8 str8_indent(Arena *arena, String8 indent, uint32_t depth);

String8 str8_copy(Arena *arena, String8 src);

String8 str8_push_format_list(Arena *arena, String8 format, va_list list);
String8 str8_pushf(Arena *arena, String8 format, ...);

String8 str8_filename(String8 path);
String8 str8_filepath_join(Arena *arena, String8 head, String8 tail);
String8 str8_directory(String8 path);

double str8_to_f64(String8 s);
uint64_t str8_to_u64(String8 s);
int64_t str8_to_s64(String8 s);
