#ifndef POOL_H
#define POOL_H

#include "common.h"

struct arena;

struct pool_element {
	struct pool_element *next;
};

struct pool {
	struct pool_element *array, *free_elements;
	uint32_t count, capacity;
	size_t element_size;
};

struct pool *allocator_pool(size_t element_size, uint32_t capacity);
struct pool *allocator_pool_from_arena(struct arena *arena, size_t element_size, uint32_t capacity);
void pool_destroy(struct pool *pool);

void *pool_push(struct pool *pool);
void *pool_push_zero(struct pool *pool);

void pool_free(struct pool *pool, void *element);

#endif
