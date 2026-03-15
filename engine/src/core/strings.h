#pragma once

#include "common.h"
#include "arena.h"

typedef struct String {
	char *memory;
	size_t length;
} String;

#define SFMT "%.*s"
#define SARG(s) (int)(s).length, (s).memory
#define S(s) ((String){ (char *)(s), sizeof(s) - 1 })
#define shash(s) string_hash64(S(s))

String string_wrap(const char *s);

ENGINE_API bool string_equals(String a, String b);
ENGINE_API bool string_equals_ignore_case(String a, String b);
ENGINE_API bool string_has_prefix(String str, String prefix);
ENGINE_API bool string_has_suffix(String str, String suffix);

int64_t string_find_first(String haystack, String needle);
int64_t string_find_last(String haystack, String needle);

String string_slice(String str, uint32_t start, uint32_t length);

String string_chop_left(String str, uint32_t n);
String string_chop_right(String str, uint32_t n);

String string_trim(String s);
String string_trim_left(String s);
String string_trim_right(String s);

ENGINE_API uint64_t string_hash64(String s);

uint32_t string_to_u32(String str);
uint64_t string_to_u64(String s);

int32_t string_to_i32(String s);
int64_t string_to_i64(String s);

float string_to_f32(String s);
double string_to_f64(String s);

ENGINE_API String stringpath_directory(String path);
ENGINE_API String stringpath_filename(String path);
ENGINE_API String stringpath_extension(String path);

ENGINE_API String string_path_join(Arena *arena, String head, String tail);
ENGINE_API String string_path_normalize(Arena *arena, String path);

ENGINE_API String string_copy(Arena *arena, String s);

ENGINE_API String string_format(Arena *arena, const char *format, ...);
ENGINE_API String string_formatv(Arena *arena, const char *format, va_list args);

ENGINE_API String string_concat(Arena *arena, String head, String tail);
ENGINE_API String string_replace(Arena *arena, String source, String find, String replace);
ENGINE_API String string_upper(Arena *arena, String s);
ENGINE_API String string_lower(Arena *arena, String s);

ENGINE_API char *string_cstring(Arena *arena, String s);

typedef struct StringNode {
	struct StringNode *next;
	String string;
} StringNode;

typedef struct StringList {
	StringNode *first;
	StringNode *last;
	size_t count;
	size_t total_length;
} StringList;

ENGINE_API void stringlist_push(Arena *arena, StringList *list, String s);
ENGINE_API StringList stringlist_split(Arena *arena, String str, String separator);
ENGINE_API String stringlist_join(Arena *arena, StringList *list, String separator);
