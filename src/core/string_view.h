#pragma once

#include "common.h"

struct arena;

typedef struct {
	uint8_t *data;
	uint32_t length;
} StringView;

#define SV_LITERAL(s) (StringView){ (uint8_t *)s, sizeof(s) - 1 }

bool string_view_equals(StringView a, StringView b);
uint64_t string_view_hash64(StringView s);
StringView string_view_copy(struct arena *arena, StringView s);
StringView string_view_concat(struct arena *arena, StringView head, StringView tail);
