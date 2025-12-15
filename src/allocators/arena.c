#include "arena.h"
#include "common.h"
#include "core/logger.h"
#include <string.h>

static Arena scratch_arenas[2] = { 0 };

Arena arena_create(size_t size) {
	Arena arena = { 0 };
	arena.offset = 0;
	arena.capacity = size;
	arena.buffer = malloc(size);
	return arena;
}

Arena arena_create_from_memory(void *buffer, size_t offset, size_t size) {
	Arena arena = { 0 };
	arena.offset = 0;
	arena.capacity = size;
	arena.buffer = (uint8_t *)buffer + offset;
	return arena;
}

void arena_clear(Arena *arena) {
	arena->offset = 0;
}
void arena_free(Arena *arena) {
	free(arena->buffer);
	free(arena);
}

void *arena_push(Arena *arena, size_t size) {
	if (arena->offset + size >= arena->capacity) {
		LOG_ERROR("ARENA_OUT_OF_MEMORY");
		exit(1);
		// return NULL;
	}

	void *result = (uint8_t *)arena->buffer + arena->offset;
	arena->offset += size;

	return result;
}

void *arena_push_zero(Arena *arena, size_t size) {
	if (arena->offset + size >= arena->capacity) {
		LOG_ERROR("ARENA_OUT_OF_MEMORY");
		exit(1);
		// return NULL;
	}

	void *result = (uint8_t *)arena->buffer + arena->offset;
	memset(result, 0, size);
	arena->offset += size;

	return result;
}

ArenaTemp arena_begin_temp(Arena *arena) {
	return (ArenaTemp){ .arena = arena, .position = arena_size(arena) };
}
void arena_end_temp(ArenaTemp temp) {
	arena_set(temp.arena, temp.position);
}

ArenaTemp arena_get_scratch(Arena *conflict) {
	if (!scratch_arenas[0].buffer)
		scratch_arenas[0] = arena_create(MiB(4));
	if (!scratch_arenas[1].buffer)
		scratch_arenas[1] = arena_create(MiB(4));

	ArenaTemp temp = {
		.arena = conflict == &scratch_arenas[0] ? &scratch_arenas[1] : &scratch_arenas[0],
	};
	temp.position = arena_size(temp.arena);

	return temp;
}

void arena_pop(Arena *arena, size_t size) {
	arena->offset -= size;
}
void arena_set(Arena *arena, size_t position) {
	arena->offset = position;
}

size_t arena_size(Arena *arena) {
	return arena->offset;
}
