#include "pool.h"

#include "arena.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>

struct pool *allocator_pool(size_t element_size, uint32_t capacity) {
	struct pool *pool = malloc(sizeof(struct pool));
	pool->array = pool->free_elements = malloc(element_size * capacity);

	pool->element_size = element_size;
	pool->capacity = capacity;
	pool->count = 0;

	for (uint32_t index = 0; index < capacity - 1; ++index) {
		struct pool_element *element = (struct pool_element *)((uint8_t *)pool->array + element_size * index);
		element->next = (struct pool_element *)((uint8_t *)element + element_size);
	}
	struct pool_element *last = (struct pool_element *)((uint8_t *)pool->array + (element_size * (capacity - 1)));
	last->next = NULL;

	return pool;
}

struct pool *allocator_pool_from_arena(struct arena *arena, size_t element_size, uint32_t capacity) {
	struct pool *pool = arena_push_type(arena, struct pool);
	pool->array = pool->free_elements = arena_push(arena, element_size * capacity);
	pool->element_size = element_size;

	for (uint32_t index = 0; index < capacity; ++index) {
		struct pool_element *element = (struct pool_element *)((uint8_t *)pool->array + element_size * index);
		struct pool_element *next = (struct pool_element *)((uint8_t *)element + element_size);

		element->next = next;
	}
	struct pool_element *last = (struct pool_element *)((uint8_t *)pool->array + (element_size * (capacity - 1)));
	last->next = NULL;

	return pool;
}

void pool_destroy(struct pool *pool) {
	if (pool) {
		free(pool->array);
		free(pool);
	}
}

void *pool_push(struct pool *pool) {
	if (pool == NULL || pool->free_elements == NULL) {
		LOG_WARN("Allocator: pool invalid or out of space");
		return NULL;
	}

	struct pool_element *element = pool->free_elements;
	pool->free_elements = pool->free_elements->next;

	return element;
}

void *pool_push_zero(struct pool *pool) {
	if (pool == NULL || pool->free_elements == NULL) {
		LOG_WARN("Allocator: invalid parameter or out of space");
		return NULL;
	}

	struct pool_element *element = pool->free_elements;
	pool->free_elements = pool->free_elements->next;

	memset(element, 0, pool->element_size);

	return element;
}

void pool_free(struct pool *pool, void *element) {
	if (pool == NULL || element == NULL) {
		LOG_WARN("Allocator: invalid paramters");
		return;
	}

	struct pool_element *freed_element = element;

	freed_element->next = pool->free_elements;
	pool->free_elements = freed_element;
}
