#include "pool.h"
#include "core/arena.h"
#include "core/debug.h"

#define POOL_HEADER(ptr) ((Pool *)ptr - 1)

typedef struct Pool {
	uint32_t *free_indices;
	void *slots;

	uint32_t free_count;
	uint32_t capacity;
	uint32_t stride;
	uint32_t used_count;
} Pool;

void *pool_create(Arena *arena, uint32_t stride, uint32_t align, uint32_t capacity, bool zero_memory) {
	ASSERT(sizeof(Pool) % align == 0);

	arena_push(arena, 0, align, false);
	Pool *pool = arena_push_struct(arena, Pool);
	pool->slots = arena_push(arena, stride * capacity, align, zero_memory);
	pool->free_indices = arena_push(arena, sizeof(*pool->free_indices) * capacity, alignof(uint32_t), true);

	pool->capacity = capacity;
	pool->free_count = capacity;
	pool->stride = stride;
	pool->used_count = 0;

	for (uint32_t i = 0; i < capacity; i++)
		pool->free_indices[i] = (capacity - 1) - i;

	return pool->slots;
}

void *pool_alloc(void *ptr) {
	Pool *pool = POOL_HEADER(ptr);

	if (pool->free_count == 0) {
		ASSERT_MESSAGE(false, "POOL_OUT_OF_MEMORY");
		return NULL;
	}

	// Pop from stack
	pool->free_count--;
	uint32_t slot_index = pool->free_indices[pool->free_count];

	pool->used_count++;

	return (uint8_t *)pool->slots + (slot_index * pool->stride);
}

uint32_t pool_index_of(void *ptr, void *slot) {
	Pool *pool = POOL_HEADER(ptr);

	uint32_t offset = (uint32_t)((uint8_t *)slot - (uint8_t *)pool->slots);
	uint32_t index = offset / pool->stride;

	ASSERT(index < pool->capacity);

	return index;
}

void pool_free(void *ptr, void *slot) {
	if (slot == NULL)
		return;
	Pool *pool = POOL_HEADER(ptr);

	uint32_t offset = (uint32_t)((uint8_t *)slot - (uint8_t *)pool->slots);
	uint32_t index = offset / pool->stride;

	ASSERT(index < pool->capacity);

	if (index >= pool->capacity)
		return;

	pool->free_indices[pool->free_count] = index;
	pool->free_count++;
	pool->used_count--;
}
