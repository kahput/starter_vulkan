#pragma once

#include "common.h"

typedef struct arena Arena;
typedef struct {
	struct arena *arena;
	uint32_t position;
} ArenaTemp;

struct arena *allocator_arena(void);
void arena_clear(struct arena *arena);
void arena_destroy(struct arena *arena);

void *arena_push(struct arena *arena, size_t size);
void *arena_push_zero(struct arena *arena, size_t size);
#define arena_push_array(arena, type, count) (type *)arena_push((arena), sizeof(type) * (count))
#define arena_push_array_zero(arena, type, count) (type *)arena_push_zero((arena), sizeof(type) * (count))
#define arena_push_type(arena, type) (type *)arena_push((arena), sizeof(type));
#define arena_push_type_zero(arena, type) (type *)arena_push_zero((arena), sizeof(type));

void arena_pop(struct arena *arena, size_t size);
void arena_set(struct arena *arena, size_t position);

ArenaTemp arena_begin_temp(struct arena *arena);
void arena_end_temp(ArenaTemp temp);

ArenaTemp arena_get_scratch(Arena **arena);
#define arena_reset_scratch(t) arena_end_temp(t)

size_t arena_size(struct arena *arena);
