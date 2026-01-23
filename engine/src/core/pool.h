#ifndef POOL_H
#define POOL_H

#include "core/arena.h"

struct arena;

typedef struct pool_slot {
	struct pool_slot *next;
} PoolSlot;

typedef struct pool {
	PoolSlot *slots, *free_slots;
	size_t slot_size;
} Pool;

void *pool_create(size_t slot_size, uint32_t capcity);
void *pool_create_frm_arena(Arena *arena, uint32_t capacity, size_t slot_size, size_t alignment);
Pool *allocator_pool(size_t slot_size, uint32_t capacity);
Pool *allocator_pool_from_arena(Arena *arena, uint32_t capacity, size_t slot_size, size_t alignment);
void pool_destroy(Pool *pool);

void *pool_alloc(Pool *pool);
void *pool_alloc_zeroed(Pool *pool);

void pool_free(Pool *pool, void *element);

#endif
