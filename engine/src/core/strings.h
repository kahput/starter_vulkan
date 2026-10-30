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
INLINE string8 str8_range(void *start, void *end) { return (string8){ .bytes = (uint8_t *)start, .length = end && start ? (end > start ? (uint8_t *)end - (uint8_t *)start : 0) : 0 }; }

INLINE string8 str8_chop(string8 s, uint32_t amt) { return str8(s.bytes, s.length - MIN(s.length, amt)); }
INLINE string8 str8_skip(string8 s, uint32_t amt) { return str8(s.bytes + MIN(amt, s.length), s.length - MIN(amt, s.length)); }
INLINE bool str8_match(string8 a, string8 b) { return a.length != b.length ? false : (a.bytes == b.bytes ? true : memcmp(a.bytes, b.bytes, a.length) == 0); }

bool str8_startswith(string8 haystack, string8 needle);
bool str8_contains(string8 haystack, string8 needle);

string8 str8_concat(Arena *arena, string8 a, string8 b);
string8 str8_indent(Arena *arena, string8 indent, uint32_t depth);
string8 str8_dedent(Arena *arena, string8 s);

string8 str8_upper(Arena *arena, string8 s);
string8 str8_lower(Arena *arena, string8 s);
string8 str8_copy(Arena *arena, string8 src);

string8 str8_fmtv(Arena *arena, const char *fmt, va_list list);
string8 str8_fmt(Arena *arena, const char *fmt, ...);

string8 str8_pathfile(string8 path);
string8 str8_pathext(string8 file);
INLINE string8 str8_pathstem(string8 path) {
	string8 ext = str8_pathext(path);
	return str8_range(str8_pathfile(path).bytes, ext.bytes ? ext.bytes - 1 : 0);
}
string8 str8_pathjoin(Arena *arena, string8 head, string8 tail);
string8 str8_pathdir(string8 path);

INLINE uint64_t str8_hash(string8 s) { return hash64(s.bytes, s.length); }
uint32_t str8_to_u32(string8 s);
double str8_to_f64(string8 s);
uint64_t str8_to_u64(string8 s);
int64_t str8_to_s64(string8 s);

typedef struct {
	uint32_t codepoint, length;
} UTF8Result;
UTF8Result utf8_decode(string8 message, uint64_t at);
uint32_t utf8_next(string8 message, uint64_t *at);
