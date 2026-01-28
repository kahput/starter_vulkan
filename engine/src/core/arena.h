#pragma once

#include "common.h"
typedef struct arena {
	size_t offset, capacity;
	void *memory;
} Arena;
typedef struct {
	struct arena *arena;
	size_t position;
} ArenaTemp;

Arena arena_create(size_t size);
Arena arena_create_from_memory(void *buffer, size_t size);
void arena_destroy(Arena *arena);

ENGINE_API void *arena_push(Arena *arena, size_t size, size_t alignment, bool zero_memory);

void arena_pop(Arena *arena, size_t size);
void arena_set(Arena *arena, size_t position);

size_t arena_size(Arena *arena);
void arena_clear(Arena *arena);

ENGINE_API ArenaTemp arena_begin_temp(Arena *);
ENGINE_API void arena_end_temp(ArenaTemp temp);

ENGINE_API ArenaTemp arena_scratch(Arena *conflict);
#define arena_release_scratch(scratch) arena_end_temp(scratch)

#define arena_push_array(arena, type, count) ((type *)arena_push((arena), sizeof(type) * (count), alignof(type), false))
#define arena_push_array_zero(arena, type, count) ((type *)arena_push((arena), sizeof(type) * (count), alignof(type), true))
#define arena_push_struct(arena, type) ((type *)arena_push((arena), sizeof(type), alignof(type), false))
#define arena_push_struct_zero(arena, type) ((type *)arena_push((arena), sizeof(type), alignof(type), true))

#define arena_push_pool(arena, type, capacity) \
	pool_create(arena, sizeof(type), alignof(type), capacity, false)
#define arena_push_pool_zero(arena, type, capacity) \
	pool_create(arena, sizeof(type), alignof(type), capacity, true)
