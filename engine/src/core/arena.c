#include "arena.h"

#include "common.h"

#include "core/debug.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>

static Arena scratch_arenas[2] = { 0 };

Arena arena_make(size_t size) { return (Arena){ .memory = malloc(size), .capacity = size }; }

void arena_destroy(Arena *arena) {
	if (arena->memory)
		free(arena->memory);

	arena->memory = NULL;
	arena->offset = 0;
	arena->capacity = 0;
}

void *arena_push(Arena *arena, size_t size, size_t alignment, bool zero_memory) {
	ASSERT(alignment > 0 && ((alignment & (alignment - 1)) == 0));
	uintptr_t current = (uintptr_t)arena->memory + arena->offset;
	uintptr_t aligned = aligned_address(current, alignment);

	size_t padding = aligned - current;

	if (arena->offset + padding + size > arena->capacity) {
		ASSERT_MESSAGE(false, "ARENA_OUT_OF_MEMORY");
		return NULL;
	}

	if (zero_memory)
		memset((void *)aligned, 0, size);

	arena->offset += padding + size;
	return (void *)aligned;
}

void arena_pop(Arena *arena, size_t size) {
	arena->offset = size > arena->offset ? 0 : arena->offset - size;
}

void arena_set(Arena *arena, size_t position) {
	arena->offset = position > arena->capacity ? arena->capacity : position;
}

size_t arena_size(Arena *arena) {
	return arena->offset;
}

void arena_clear(Arena *arena) {
	arena->offset = 0;
}

ArenaTemp arena_temp_begin(Arena *arena) {
	return (ArenaTemp){ .arena = arena, .position = arena_size(arena) };
}

void arena_temp_end(ArenaTemp temp) {
	arena_set(temp.arena, temp.position);
}

ArenaTemp arena_scratch(Arena *conflict) {
	if (scratch_arenas[0].memory == NULL) {
		// TODO: Lower this back down
		scratch_arenas[0] = arena_make(MiB(256));
		scratch_arenas[1] = arena_make(MiB(256));
	}

	Arena *selected = conflict == &scratch_arenas[0] ? &scratch_arenas[1] : &scratch_arenas[0];
	return arena_temp_begin(selected);
}
typedef struct ArenaPool {
	void *first_free;
	void *tracker;
	uint32_t capacity;
	uint32_t stride;
} ArenaPool;

#define POOL_HEADER(ptr) ((ArenaPool *)(ptr) - 1)
void *_arena_push_pool(Arena *arena, uint32_t stride, uint32_t align, uint32_t capacity, bool zero_memory) {
	size_t header_align = alignof(ArenaPool) > align ? alignof(ArenaPool) : align;

	uint8_t *memory = (uint8_t *)arena_push(arena, sizeof(ArenaPool) + (stride * capacity), header_align, zero_memory);

	ArenaPool *pool = (ArenaPool *)memory;
	void *slots = (void *)(pool + 1);

	pool->tracker = arena_push(arena, sizeof(void *) * capacity, alignof(void *), false);
	pool->first_free = arena_slist_make(pool->tracker, sizeof(void *), capacity);

	pool->capacity = capacity;
	pool->stride = stride;

	return slots;
}

void *arena_pool_alloc(void *slots) {
	if (slots == NULL)
		return NULL;
	ArenaPool *pool = POOL_HEADER(slots);

	void *node = arena_slist_alloc(&pool->first_free);
	if (node == NULL) {
		LOG_ERROR("Pool is out of memory!");
		ASSERT(false);
		return NULL;
	}

	uint32_t index = ((uint8_t *)node - (uint8_t *)pool->tracker) / sizeof(void *);
	return (uint8_t *)slots + (index * pool->stride);
}

void arena_pool_free(void *slots, void *slot) {
	if (slots == NULL || slot == NULL)
		return;
	ArenaPool *pool = POOL_HEADER(slots);

	uint32_t index = ((uint8_t *)slot - (uint8_t *)slots) / pool->stride;

	if (index >= pool->capacity) {
		LOG_ERROR("Attempted to free a slot outside of pool!");
		return;
	}

	void *node = (uint8_t *)pool->tracker + (index * sizeof(void *));

	arena_slist_free(&pool->first_free, node);

	memset(slot, 0, pool->stride);
}

typedef struct slist_node {
	struct slist_node *next;
} SListNode;

void *arena_slist_make(void *array, size_t stride, uint32_t capacity) {
	for (uint32_t index = 0; index < capacity; ++index) {
		SListNode *element = (SListNode *)((uint8_t *)array + stride * index);
		element->next = (SListNode *)((uint8_t *)element + stride);

		if (index + 1 == capacity)
			element->next = NULL;
	}

	return array;
}

void *arena_slist_alloc(void **first_free) {
	if (first_free == NULL || *first_free == NULL)
		return NULL;

	SListNode *element = (SListNode *)*first_free;
	*first_free = element->next;

	return element;
}

void arena_slist_free(void **first_free, void *slot) {
	if (first_free == NULL || *first_free == NULL)
		return;

	SListNode *head = *first_free;
	SListNode *element = slot;

	element->next = head;
	*first_free = element;
}

void *arena_trie_ensure(Arena *arena, ArenaTrieNode **root, uint64_t hash, size_t size, size_t align, ArenaTrieMode mode) {
	ArenaTrieNode **node = root;

	for (uint64_t hash_index = hash; *node; hash_index <<= 2) {
		if (hash == (*node)->hash) {
			return (*node)->payload;
		}
		node = &(*node)->children[hash_index >> 62];
	}

	if (arena == NULL)
		return NULL;

	switch (mode) {
		case ARENA_TRIE_INTRUSIVE: {
			*node = arena_push(arena, size, align, true);
			(*node)->hash = hash;
			(*node)->payload = *node;
			return (*node)->payload;
		}
		case ARENA_TRIE_SEQUENTIAL: {
			*node = arena_push(arena, size + sizeof(ArenaTrieNode), align, true);
			(*node)->hash = hash;
			(*node)->payload = *node + 1;
			return (*node)->payload;
		}
		default: { // ARENA_TRIE_NODE_ONLY
			*node = arena_push(arena, size, align, true);
			(*node)->hash = hash;
			(*node)->payload = NULL;
			return *node; // The only time we return the raw node
		}
	}
}
