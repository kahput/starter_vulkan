#pragma once

#include "common.h"

typedef struct arena {
	size_t offset, capacity;
	void *buffer;
} Arena;
typedef struct {
	struct arena *arena;
	uint32_t position;
} ArenaTemp;

Arena arena_create(size_t size);
Arena arena_create_from_memory(void *buffer, size_t offset, size_t size);
void arena_clear(Arena *);
void arena_destroy(Arena *);

void *arena_push(Arena *arena, size_t size);
void *arena_push_zero(Arena *, size_t size);
#define arena_push_array(arena, type, count) (type *)arena_push((arena), sizeof(type) * (count))
#define arena_push_array_zero(arena, type, count) (type *)arena_push_zero((arena), sizeof(type) * (count))
#define arena_push_struct(arena, type) (type *)arena_push((arena), sizeof(type));
#define arena_push_struct_zero(arena, type) (type *)arena_push_zero((arena), sizeof(type));

void arena_pop(Arena *, size_t size);
void arena_set(Arena *, size_t position);

ArenaTemp arena_begin_temp(Arena *);
void arena_end_temp(ArenaTemp temp);

ArenaTemp arena_scratch(Arena *conflict);
#define arena_release_scratch(t) arena_end_temp(t)

size_t arena_size(Arena *);
