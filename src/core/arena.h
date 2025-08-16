#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct arena Arena;

struct arena *arena_alloc(void);
void arena_clear(struct arena *arena);
void arena_free(struct arena *arena);

void *arena_push(struct arena *arena, size_t size);
void *arena_push_zero(struct arena *arena, size_t size);
#define arena_push_array(arena, type, count) (type *)arena_push((arena), sizeof(type) * (count))
#define arena_push_array_zero(arena, type, count) (type *)arena_push_zero((arena), sizeof(type) * (count))
#define arena_push_type(arena, type) (type *)arena_push((arena), sizeof(type));
#define arena_push_type_zero(arena, type) (type *)arena_push_zero((arena), sizeof(type));

void arena_pop(struct arena *arena, size_t size);
void arena_set(struct arena *arena, size_t position);

size_t arena_size(struct arena *arena);
