#pragma once

#include "common.h"
#include "arena.h"

typedef struct String {
	char *memory;
	size_t length;
} String;

#define str_expand(s) (int)(s).length, (s).memory
#define str_lit(s) ((String){ (char *)(s), sizeof(s) - 1 })

String string_from_cstr(const char *s);
String string_from_range(char *start, char *end);

bool string_equals(String a, String b);
bool string_equals_ignore_case(String a, String b);
bool string_has_prefix(String str, String prefix);
bool string_has_suffix(String str, String suffix);

int64_t string_find_first(String haystack, String needle);
int64_t string_find_last(String haystack, String needle);

String string_slice(String str, uint32_t start, uint32_t length);

String string_chop_left(String str, uint32_t n);
String string_chop_right(String str, uint32_t n);

String string_trim(String s);
String string_trim_left(String s);
String string_trim_right(String s);

uint64_t string_hash64(String s);

int64_t string_to_i64(String s);
double string_to_f64(String s);

String string_path_folder(String path);
String string_path_filename(String path);
String string_path_extension(String path);

String string_push_copy(Arena *arena, String s);

String string_pushf(Arena *arena, const char *format, ...);
String string_pushfv(Arena *arena, const char *format, va_list args);

String string_push_concat(Arena *arena, String head, String tail);
String string_push_replace(Arena *arena, String source, String find, String replace);
String string_push_upper(Arena *arena, String s);
String string_push_lower(Arena *arena, String s);

String string_push_path_join(Arena *arena, String head, String tail);
char *string_push_cstring(Arena *arena, String s);

typedef struct StringNode {
	struct StringNode *next;
	String string;
} StringNode;

typedef struct StringList {
	StringNode *first;
	StringNode *last;
	size_t node_count;
	size_t total_length;
} StringList;

void string_list_push(Arena *arena, StringList *list, String s);
StringList string_list_split(Arena *arena, String str, String separator);
String string_list_join(Arena *arena, StringList *list, String separator);
