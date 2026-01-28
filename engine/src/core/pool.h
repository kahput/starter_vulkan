#ifndef POOL_H_
#define POOL_H_

#include "arena.h"

void *pool_create(Arena *arena, uint32_t stride, uint32_t align, uint32_t capacity, bool zero_memory);

#define pool_alloc_struct(pool, type) (type *)pool_alloc(pool)

void *pool_alloc(void *pool);
uint32_t pool_index_of(void *pool, void *slot);
void pool_free(void *pool, void *slot);

#endif
