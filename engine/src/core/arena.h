#pragma once

#include "common.h"
#include <stdatomic.h>
typedef struct arena {
	size_t offset, capacity;
	void *memory;
} Arena;
typedef struct {
	struct arena *arena;
	size_t position;
} ArenaTemp;

Arena arena_make(size_t size);
Arena *arena_partition(Arena *arena, size_t size);
static inline Arena arena_wrap(void *buffer, size_t size) { return (Arena){ .memory = buffer, .capacity = size }; }
void arena_destroy(Arena *arena);

#define arena_wrap_struct(s) \
	(Arena) { .memory = s, .capacity = sizeof(*s) }
#define arena_wrap_array(array) \
	(Arena) { .memory = array, .capacity = sizeof((array)) }
#define arena_wrap_count(memory, count) \
	(Arena) { .memory = memory, .capacity = sizeof(*(memory)) * count }

ENGINE_API void *arena_push(Arena *arena, size_t size, size_t align, bool zero);
void *arena_push_copy(Arena *arena, void *src, size_t size);

void arena_pop(Arena *arena, size_t size);
void arena_set(Arena *arena, size_t position);

size_t arena_size(Arena *arena);
void arena_clear(Arena *arena);

ENGINE_API ArenaTemp arena_temp_begin(Arena *arena);
ENGINE_API void arena_temp_end(ArenaTemp temp);

ENGINE_API ArenaTemp arena_scratch(Arena *conflict);
#define arena_scratch_release(scratch) arena_temp_end(scratch)

#define arena_put(arena, T, ...) (void)(*(T *)arena_push((arena), sizeof(T), alignof(16), false) = (T)__VA_ARGS__)
#define arena_push_count(arena, count, T) ((T *)arena_push((arena), sizeof(T) * (count), alignof(T), true))
#define arena_push_struct(arena, T) ((T *)arena_push((arena), sizeof(T), alignof(T), true))
#define arena_push_pool(arena, capacity, T) pool_create(arena, sizeof(T), alignof(T), capacity, true)

void *arena_list_make(void *buffer, size_t stride, uint32_t capacity);

void *arena_list_alloc(void **list);
void arena_list_free(void **list, void *node);

#define arena_push_list(arena, type, capacity) \
	arena_list_make(arena_push_array((arena), type, capacity), sizeof(type), (capacity))

#define arena_list_pop(list, type) (type *)arena_list_alloc((void **)list)
#define arena_list_push(list, node) arena_list_free((void **)list, node)

typedef struct arena_trie_node {
	struct arena_trie_node *children[4];
	uint64_t hash;

	void *payload;
	const char *debug_type_name;
} ArenaTrieNode;

typedef struct {
	Arena *arena;
	ArenaTrieNode *root;
} ArenaTrie;

static inline ArenaTrie arena_trie_make(Arena *arena) { return (ArenaTrie){ .arena = arena }; }
ENGINE_API ArenaTrieNode *arena_trienode_ensure(Arena *arena, ArenaTrieNode **root, uint64_t hash, const char *debug_type_name);
ENGINE_API void *arena_trie_ensure(Arena *arena, ArenaTrieNode **root, uint64_t hash, size_t size, size_t align, bool intrusive, const char *debug_type_name);

#define arena_trie_wrap_array(arr)                                                  \
	(ArenaTrie) {                                                                   \
		.arena = &(Arena){ .offset = 0, .capacity = sizeof(arr), .memory = (arr) }, \
		.root = NULL                                                                \
	}

#define arena_trienode_push(trie, hash) (arena_trienode_ensure((trie)->arena, &(trie)->root, (hash), NULL))
#define arena_trienode_find(trie, hash) (arena_trienode_ensure(NULL, &(trie)->root, (hash), NULL))

#define arena_trie_push_count(trie, hash, count, T) ((T *)arena_trie_ensure((trie)->arena, &(trie)->root, (hash), sizeof(T) * count, alignof(T), false, #T))
#define arena_trie_push(trie, hash, T) ((T *)arena_trie_ensure((trie)->arena, &(trie)->root, (hash), sizeof(T), alignof(T), false, #T))
#define arena_trie_pushi(trie, hash, T) ((T *)arena_trie_ensure((trie)->arena, &(trie)->root, (hash), sizeof(T), alignof(T), true, #T))
#define arena_trie_put(trie, hash, T, ...) (void)(*(T *)arena_trie_ensure((trie)->arena, &(trie)->root, (hash), sizeof(T), alignof(T), false, #T) = (T)__VA_ARGS__)
#define arena_trie_find(trie, hash, T) ((T *)arena_trie_ensure(NULL, &(trie)->root, (hash), 0, 0, 0, #T))

#define arena_trieset_push(trie, hash) (arena_trienode_ensure((trie)->arena, &(trie)->root, (hash), NULL))
#define arena_trieset_find(trie, hash) (arena_trienode_ensure(NULL, &(trie)->root, (hash), NULL))
