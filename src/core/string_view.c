#include "strings.h"

#include "allocators/arena.h"

#include <string.h>

bool string_view_equals(StringView a, StringView b) {
	if (a.length != b.length)
		return false;

	return !a.length || !memcmp(a.data, b.data, a.length);
}

uint64_t string_view_hash64(StringView str) {
	uint64_t h = 0x100;

	for (uint32_t index = 0; index < str.length; ++index) {
		h ^= str.data[index] & 255;
		h *= 1111111111111111111;
	}

	return h;
}

StringView string_view_copy(struct arena *arena, StringView str) {
	StringView copy = str;
	copy.data = arena_push_array_zero(arena, uint8_t, str.length);

	if (copy.length)
		memcpy(copy.data, str.data, copy.length);
	return copy;
}

StringView string_view_concat(struct arena *arena, StringView head, StringView tail) {
	if (!head.data || head.data + head.length != arena_push(arena, 0)) {
		head = string_view_copy(arena, head);
	}
	head.length += string_view_copy(arena, tail).length;
	return head;
}
