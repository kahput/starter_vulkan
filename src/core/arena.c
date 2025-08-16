#include "arena.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct arena {
	size_t offset, capacity;
	void *data;
};

Arena *arena_alloc(void) {
	Arena *arena = malloc(sizeof(Arena));
	arena->offset = 0;
	arena->capacity = (1 << 24);
	arena->data = malloc(arena->capacity);
	return arena;
}

void arena_clear(Arena *arena) {
	arena->offset = 0;
}
void arena_free(Arena *arena) {
	free(arena->data);
	free(arena);
}

void *arena_push(Arena *arena, size_t size) {
	if (arena->offset + size >= arena->capacity) {
		fprintf(stderr, "ARENA_OUT_OF_MEMORY\n");
		exit(1);
		// return NULL;
	}

	void *result = (uint8_t *)arena->data + arena->offset;
	arena->offset += size;

	return result;
}

void *arena_push_zero(Arena *arena, size_t size) {
	if (arena->offset + size >= arena->capacity) {
		fprintf(stderr, "ARENA_OUT_OF_MEMORY\n");
		exit(1);
		// return NULL;
	}

	void *result = (uint8_t *)arena->data + arena->offset;
	memset(result, 0, size);
	arena->offset += size;

	return result;
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
