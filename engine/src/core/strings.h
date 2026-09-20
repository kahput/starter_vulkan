#pragma once

#include "common.h"
#include "core/arena.h"

typedef struct {
	uint8_t *bytes;
	uint64_t length;
} string8;

#define s(s) lit8(s)
#define lit8(s) (string8){ .bytes = (uint8_t *)s, .length = sizeof(s) - 1 }
#define comp8(s) { .bytes = (uint8_t *)s, .length = sizeof(s) - 1 }
#define arg8(s) (int)s.length, (char *)s.bytes

INLINE string8 str8(void *bytes, uint64_t len) { return (string8){ .bytes = bytes, .length = len }; }
INLINE string8 str8z(const char *cstring) { return (string8){ .bytes = (uint8_t *)cstring, .length = strlen(cstring) }; }
INLINE string8 str8_range(void *start, void *end) { return (string8){ .bytes = (uint8_t *)start, .length = (uint8_t *)end - (uint8_t *)start }; }

bool eq8(string8 a, string8 b);
bool has8(string8 haystack, string8 needle);

string8 concat8(Arena *arena, string8 a, string8 b);
string8 indent8(Arena *arena, string8 indent, uint32_t depth);
string8 dedent8(Arena *arena, string8 s);

string8 upper8(Arena *arena, string8 s);
string8 lower8(Arena *arena, string8 s);
string8 copy8(Arena *arena, string8 src);

string8 fmtv8(Arena *arena, const char *fmt, va_list list);
string8 fmt8(Arena *arena, const char *fmt, ...);

string8 filename8(string8 path);
string8 ext8(string8 file);
string8 pathjoin8(Arena *arena, string8 head, string8 tail);
string8 dir8(string8 path);

INLINE uint64_t hash8(string8 s) { return hash64(s.bytes, s.length); }
double str8_to_f64(string8 s);
uint64_t str8_to_u64(string8 s);
int64_t str8_to_s64(string8 s);
