#pragma once

#include "common.h"
#include "core/arena.h"

typedef struct {
	uint8_t *text;
	uint64_t length;
} String8;

#define s(s) str_lit(s)
#define str_arg(s) s.length, s.text
#define str_lit(s) (String8){ .text = (uint8_t *)s, .length = sizeof(s) - 1 }
#define str_comp(s) { .text = (uint8_t *)s, .length = sizeof(s) - 1 }
#define str_spread(s) (int32_t)s.length, s.text

String8 str8_wrap(const char *cstring);

bool str8_equals(String8 a, String8 b);

String8 str8_concat(Arena *arena, String8 a, String8 b);
String8 str8_filepath_join(Arena *arena, String8 head, String8 tail);
String8 str8_filename(String8 path);
String8 str8_directory(String8 path);
