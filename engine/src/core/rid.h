#pragma once

#include "common.h"
#include "core/arena.h"

// [ type : 8 | generation : 16 | index : 32 ]
typedef uint64_t RID;
#define RID_INVALID (RID)0
INLINE bool rid_valid(RID id) { return id != RID_INVALID; }

#define RID_INDEX_BITS 32u
#define RID_INDEX_MASK 0xFFFFFFFFu

INLINE uint8_t rid_type(RID id) { return (uint8_t)(id >> 56); }
INLINE uint16_t rid_gen(RID id) { return (id >> RID_INDEX_BITS) & 0xFFFFu; }
INLINE uint32_t rid_index(RID id) { return (uint32_t)id; }

INLINE RID rid_make(uint64_t type, uint64_t generation, uint64_t index) {
	return (type << 56) | ((generation & 0xFFFFu) << RID_INDEX_BITS) | (index & RID_INDEX_MASK);
}
INLINE RID rid_touch(RID id) {
	return rid_make(rid_type(id), rid_gen(id) + 1, id);
}

typedef struct {
	uint32_t *indices;
	uint16_t *generations;
	uint32_t next_free;
	uint32_t type, count, capacity;
} RID_Pool;

RID_Pool rid_pool_make(Arena *arena, uint8_t type, uint32_t capacity);

RID rid_pool_add(RID_Pool *pool);
void rid_pool_remove(RID_Pool *pool, RID id);
INLINE bool rid_pool_has(const RID_Pool *pool, RID id) {
	uint32_t index = rid_index(id);
	return pool && rid_valid(id) && rid_type(id) == pool->type && index < pool->capacity && (pool->generations[index] & 1) && pool->generations[index] == rid_gen(id);
}

INLINE RID rid_pool_at(RID_Pool *pool, uint32_t index) {
	return pool && index < pool->capacity && (pool->generations[index] & 1) == 1 ? rid_make(pool->type, pool->generations[index], index) : RID_INVALID;
}

#define rid_pool_find(pool, elements, id) (rid_pool_has((pool), (id)) ? elements + rid_index((id)) : 0)
