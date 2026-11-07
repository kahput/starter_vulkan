#include "rid.h"
#include "core/debug.h"

RID_Pool rid_pool_make(Arena *arena, uint8_t type, uint32_t capacity) {
	RID_Pool result = { 0 };
	result.indices = arena_push_count(arena, uint32_t, capacity);
	result.generations = arena_push_count(arena, uint16_t, capacity);
	result.capacity = capacity;
	result.type = type;

	for (uint32_t index = 0; index < capacity; ++index)
		result.indices[index] = (capacity - 1) - index;

	return result;
}

/** Registers an object and returns its handle (RID_INVALID when the pool is full). */
RID rid_pool_add(RID_Pool *pool) {
	RID result = RID_INVALID;
	bool ok = pool && pool->next_free < pool->capacity;
	if (ok) {
		uint32_t index = pool->indices[pool->next_free++];
		result = rid_make(pool->type, ++pool->generations[index], index); // even -> odd, occupied
		pool->count++;
	}

	return result;
}
void rid_pool_remove(RID_Pool *pool, RID id) {
	uint8_t gen = rid_gen(id);
	uint32_t index = rid_index(id);

	bool ok = rid_pool_has(pool, id);
	if (ok) {
		pool->indices[--pool->next_free] = index;
		pool->generations[index] += 1;
		pool->count--;
	}
    
    ASSERT(ok);
}
