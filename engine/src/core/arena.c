#include "arena.h"

#include "common.h"

#include "core/debug.h"
#include "core/logger.h"
#include "core/memory.h"

#include <stdlib.h>

static Arena scratch_arenas[2] = { 0 };

Arena arena_create(size_t size) {
	Arena arena = { 0 };
	arena.memory = malloc(size);
	arena.capacity = size;
	return arena;
}

Arena arena_create_from_memory(void *buffer, size_t size) {
	Arena arena = { 0 };
	arena.memory = buffer;
	arena.capacity = size;
	return arena;
}

void arena_free(Arena *arena) {
	if (arena->memory)
		free(arena->memory);

	arena->memory = NULL;
	arena->offset = 0;
	arena->capacity = 0;
}

void *arena_push(Arena *arena, size_t size, size_t alignment, bool zero_memory) {
	ASSERT(alignment > 0 && ((alignment & (alignment - 1)) == 0));
	uintptr_t current = (uintptr_t)arena->memory + arena->offset;
	uintptr_t aligned = aligned_address(current, alignment);

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

void arena_pop(Arena *arena, size_t size) {
	arena->offset = size > arena->offset ? 0 : arena->offset - size;
}

void arena_set(Arena *arena, size_t position) {
	arena->offset = position > arena->capacity ? arena->capacity : position;
}

size_t arena_size(Arena *arena) {
	return arena->offset;
}

void arena_clear(Arena *arena) {
	arena->offset = 0;
}

ENGINE_API ArenaTemp arena_begin_temp(Arena *arena) {
	return (ArenaTemp){ .arena = arena, .position = arena_size(arena) };
}

void arena_end_temp(ArenaTemp temp) {
	arena_set(temp.arena, temp.position);
}

ENGINE_API ArenaTemp arena_scratch(Arena *conflict) {
	if (scratch_arenas[0].memory == NULL) {
		// TODO: Lower this back down
		scratch_arenas[0] = arena_create(MiB(256));
		scratch_arenas[1] = arena_create(MiB(256));
	}

	Arena *selected = conflict == &scratch_arenas[0] ? &scratch_arenas[1] : &scratch_arenas[0];
	return arena_begin_temp(selected);
}
