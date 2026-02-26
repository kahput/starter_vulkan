#pragma once

#include "common.h"
typedef struct arena {
	size_t offset, capacity;
	void *memory;
} Arena;
typedef struct {
	struct arena *arena;
	size_t position;
} ArenaTemp;

Arena arena_make(size_t size);
static inline Arena arena_wrap(void *buffer, size_t size) { return (Arena){ .memory = buffer, .capacity = size }; }
void arena_destroy(Arena *arena);

#define arena_wrap_array(array) ((Arena){ .memory = array, .capacity = sizeof(array) })
#define arena_wrap_struct(s) ((Arena){ .memory = s, .capacity = sizeof(*s) })

ENGINE_API void *arena_push(Arena *arena, size_t size, size_t align, bool zero);

void arena_pop(Arena *arena, size_t size);
void arena_set(Arena *arena, size_t position);

size_t arena_size(Arena *arena);
void arena_clear(Arena *arena);

ENGINE_API ArenaTemp arena_temp_begin(Arena *arena);
ENGINE_API void arena_temp_end(ArenaTemp temp);

ENGINE_API ArenaTemp arena_scratch(Arena *conflict);
#define arena_scratch_release(scratch) arena_temp_end(scratch)

#define arena_push_array(arena, type, count) ((type *)arena_push((arena), sizeof(type) * (count), alignof(type), false))
#define arena_push_array_zero(arena, type, count) ((type *)arena_push((arena), sizeof(type) * (count), alignof(type), true))
#define arena_push_struct(arena, type) ((type *)arena_push((arena), sizeof(type), alignof(type), false))
#define arena_push_struct_zero(arena, type) ((type *)arena_push((arena), sizeof(type), alignof(type), true))

#define arena_push_pool(arena, type, capacity) pool_create(arena, sizeof(type), alignof(type), capacity, false)
#define arena_push_pool_zero(arena, type, capacity) pool_create(arena, sizeof(type), alignof(type), capacity, true)

void *arena_slist_make(void *buffer, size_t stride, uint32_t capacity);

void *arena_slist_alloc(void **list);
void arena_slist_free(void **list, void *node);

#define arena_push_slist(arena, type, capacity) \
	arena_slist_make(arena_push_array((arena), type, capacity), sizeof(type), (capacity))

#define arena_slist_pop(list, type) (type *)arena_slist_alloc((void **)list)
#define arena_slist_push(list, node) arena_slist_free((void **)list, node)

typedef struct arena_trie_node {
	struct arena_trie_node *children[4];
	uint64_t hash;

	void *payload;
} ArenaTrieNode;

typedef enum {
	ARENA_TRIE_NODE_ONLY,
	ARENA_TRIE_INTRUSIVE,
	ARENA_TRIE_SEQUENTIAL,
} ArenaTrieMode;

typedef struct {
	Arena *arena;
	ArenaTrieNode *root;
} ArenaTrie;

static inline ArenaTrie arena_trie_make(Arena *arena, ArenaTrieNode *root) { return (ArenaTrie){ .arena = arena , .root = root }; }
ENGINE_API void *arena_trie_ensure(Arena *arena, ArenaTrieNode **root, uint64_t hash, size_t size, size_t align, ArenaTrieMode mode);

#define arena_trie_wrap_struct(s)                                \
	(ArenaTrie) {                                                \
		.arena = &(Arena) { .memory = s, .capacity = izeof(*s) } \
	}
#define arena_trie_wrap_array(arr)                                     \
	(ArenaTrie) {                                                      \
		.arena = &(Arena){ .memory = (arr), .capacity = sizeof(arr) }, \
	}

#define arena_trie_push(trie, hash, type) ((type *)arena_trie_ensure((trie).arena, &(trie).root, (hash), sizeof(type), alignof(type), ARENA_TRIE_SEQUENTIAL))
#define arena_trie_pushi(trie, hash, type) ((type *)arena_trie_ensure((trie).arena, &(trie).root, (hash), sizeof(type), alignof(type), ARENA_TRIE_INTRUSIVE))
#define arena_trie_find(trie, hash, type) ((type *)arena_trie_ensure(NULL, &(trie).root, (hash), 0, 0, 0))
