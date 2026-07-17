#include "arena.h"

#include "common.h"

#include "core/debug.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>

Arena scratch_arenas[2] = { 0 };

Arena arena_make(size_t size) { return (Arena){ .base = malloc(size), .capacity = size }; }

void arena_destroy(Arena *arena) {
	if (arena->base)
		free(arena->base);

	arena->base = NULL;
	arena->offset = 0;
	arena->capacity = 0;
}

void *arena_push(Arena *arena, size_t size, size_t alignment, bool zero_memory) {
	uintptr_t current = (uintptr_t)arena->base + arena->offset;
	uintptr_t aligned = alignup(current, alignment ? alignment : 1);

	size_t padding = aligned - current;

	if (arena->offset + padding + size > arena->capacity) {
		ASSERT_MESSAGE(false, "ARENA_OUT_OF_MEMORY");
		return NULL;
	}

	if (zero_memory)
		memory_zero((void *)aligned, size);

	arena->offset += padding + size;
	return (void *)aligned;
}

void *arena_push_copy(Arena *arena, void *src, size_t size, size_t align) {
	void *dst = arena_push(arena, size, align, false);
	memcpy(dst, src, size);
	return dst;
}

void arena_pop(Arena *arena, size_t size) {
	arena->offset = size > arena->offset ? 0 : arena->offset - size;
}

void arena_rewind(Arena *arena, size_t position) {
	arena->offset = position > arena->capacity ? arena->capacity : position;
}

size_t arena_mark(Arena *arena) {
	return arena->offset;
}

void arena_reset(Arena *arena) {
	memory_zero(arena->base, arena->offset);
	arena->offset = 0;
}

ArenaTemp arena_temp_begin(Arena *arena) {
	return (ArenaTemp){ .arena = arena, .position = arena_mark(arena) };
}

void arena_temp_end(ArenaTemp temp) {
	arena_rewind(temp.arena, temp.position);
}

ArenaTemp arena_scratch_begin(Arena *conflict) {
	if (scratch_arenas[0].base == NULL) {
		// TODO: Lower this back down
		scratch_arenas[0] = arena_make(MiB(16));
		scratch_arenas[1] = arena_make(MiB(16));
	}

	Arena *selected = conflict == &scratch_arenas[0] ? &scratch_arenas[1] : &scratch_arenas[0];
	return arena_temp_begin(selected);
}
