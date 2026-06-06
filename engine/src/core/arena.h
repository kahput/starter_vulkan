#pragma once

#include "common.h"
typedef struct arena {
	size_t offset, capacity;
	void *base;
} Arena;
typedef struct {
	struct arena *arena;
	size_t position;
} ArenaTemp;

Arena arena_make(size_t size);
static inline Arena arena_wrap(void *buffer, size_t size) { return (Arena){ .base = buffer, .capacity = size }; }
void arena_destroy(Arena *arena);

void *arena_push(Arena *arena, size_t size, size_t align, bool zero);
void *arena_push_copy(Arena *arena, void *src, size_t size, size_t align);
void arena_pop(Arena *arena, size_t size);

size_t arena_mark(Arena *arena);
void arena_rewind(Arena *arena, size_t position);
void arena_reset(Arena *arena);

ArenaTemp arena_temp_begin(Arena *arena);
void arena_temp_end(ArenaTemp temp);

ArenaTemp arena_scratch_begin(Arena *conflict);
static inline void arena_scratch_end(ArenaTemp scratch) { arena_temp_end(scratch); }

#define arena_push_count(a, T, c) arena_push((a), sizeof(T) * (c), MAX(8, alignof(T)), true)
