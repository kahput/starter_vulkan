#include "pool.h"

#include "arena.h"
#include "core/logger.h"

#include "core/debug.h"
#include <stdlib.h>
#include <string.h>

Pool *allocator_pool(size_t slot_size, uint32_t capacity) {
	if (capacity == 0 || slot_size < sizeof(size_t)) {
		LOG_WARN("Allocator: pool size must fit size_t pointer");
		return NULL;
	}

	Pool *pool = malloc(sizeof(struct pool) + slot_size * capacity);
	pool->slots = pool->free_slots = (PoolSlot *)(pool + 1);
	pool->slot_size = slot_size;

	for (uint32_t index = 0; index < capacity - 1; ++index) {
		PoolSlot *element = (PoolSlot *)((uint8_t *)pool->slots + slot_size * index);
		element->next = (PoolSlot *)((uint8_t *)element + slot_size);
	}
	PoolSlot *last = (PoolSlot *)((uint8_t *)pool->slots + (slot_size * (capacity - 1)));
	last->next = NULL;

	return pool;
}

Pool *allocator_pool_from_arena(Arena *arena, uint32_t capacity, size_t slot_size) {
	if (capacity == 0 || slot_size < sizeof(size_t)) {
		LOG_WARN("Allocator: pool size must fit size_t pointer");
		return NULL;
	}

	Pool *pool = arena_push_struct(arena, Pool);
	pool->slots = pool->free_slots = arena_push(arena, slot_size * capacity);
	pool->slot_size = slot_size;

	for (uint32_t index = 0; index < capacity; ++index) {
		PoolSlot *element = (PoolSlot *)((uint8_t *)pool->slots + slot_size * index);
		PoolSlot *next = (PoolSlot *)((uint8_t *)element + slot_size);

		element->next = next;
	}
	PoolSlot *last = (PoolSlot *)((uint8_t *)pool->slots + (slot_size * (capacity - 1)));
	last->next = NULL;

	return pool;
}

void pool_destroy(Pool *pool) {
	if (pool) {
		free(pool->slots);
		free(pool);
	}
}

void *pool_alloc(Pool *pool) {
	if (pool == NULL || pool->free_slots == NULL) {
		LOG_WARN("Allocator: pool invalid or out of space");
		ASSERT(false);
		return NULL;
	}

	PoolSlot *element = pool->free_slots;
	pool->free_slots = element->next;

	return element;
}

void *pool_alloc_zeroed(Pool *pool) {
	if (pool == NULL || pool->free_slots == NULL) {
		LOG_WARN("Allocator: invalid parameter or out of space");
		return NULL;
	}

	PoolSlot *element = pool->free_slots;
	pool->free_slots = pool->free_slots->next;

	memset(element, 0, pool->slot_size);

	return element;
}

void pool_free(Pool *pool, void *element) {
	if (pool == NULL || element == NULL) {
		LOG_WARN("Allocator: invalid paramters");
		return;
	}

	PoolSlot *freed_element = element;

	freed_element->next = pool->free_slots;
	pool->free_slots = freed_element;
}
