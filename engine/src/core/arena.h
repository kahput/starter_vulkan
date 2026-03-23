#pragma once

#include "common.h"
typedef struct arena {
	size_t offset, capacity;
	void *base;
} Arena;
typedef struct {
	struct arena *arena;
	size_t position;
} ArenaTemp;

Arena arena_make(size_t size);
Arena *arena_partition(Arena *arena, size_t size);
static inline Arena arena_wrap(void *buffer, size_t size) { return (Arena){ .base = buffer, .capacity = size }; }
void arena_destroy(Arena *arena);

#define arena_wrap_struct(s) \
	(Arena) { .base = (s), .capacity = sizeof(*(s)) }
#define arena_wrap_array(array) \
	(Arena) { .base = (array), .capacity = sizeof((array)) }
#define arena_wrap_count(ptr, count) \
	(Arena) { .base = (ptr), .capacity = sizeof(*(ptr)) * (count) }

ENGINE_API void *arena_push(Arena *arena, size_t size, size_t align, bool zero);
ENGINE_API void *arena_push_copy(Arena *arena, void *src, size_t size, size_t align);
void arena_pop(Arena *arena, size_t size);

size_t arena_mark(Arena *arena);
void arena_rewind(Arena *arena, size_t position);
void arena_reset(Arena *arena);

ENGINE_API ArenaTemp arena_temp_begin(Arena *arena);
ENGINE_API void arena_temp_end(ArenaTemp temp);

ENGINE_API ArenaTemp arena_scratch_begin(Arena *conflict);
#define arena_scratch_end(scratch) arena_temp_end(scratch)

#define arena_put(arena, T, ...)                                        \
	do {                                                                \
		T _val = __VA_ARGS__;                                           \
		*(T *)arena_push((arena), sizeof(T), alignof(T), false) = _val; \
	} while (0)
#define arena_push_count(arena, count, T) ((T *)arena_push((arena), sizeof(T) * (count), alignof(T), true))
#define arena_push_struct(arena, T) ((T *)arena_push((arena), sizeof(T), alignof(T), true))
#define arena_push_pool(arena, capacity, T) pool_create((arena), sizeof(T), alignof(T), capacity, true)

typedef struct arena_trie_node {
	struct arena_trie_node *children[4];
	const char *debug_type_name;
	size_t key_size;
	// Key is stored at: (uint8_t *)node + sizeof(ArenaTrieNode)

	void *payload;
} ArenaTrieNode;

typedef struct arena_trie_header {
	struct arena_trie_header *children[4];
} ArenaTrieHeader;

typedef struct {
	Arena *arena;
	ArenaTrieNode *root;
} ArenaTrie;

static inline ArenaTrie arena_trie_make(Arena *arena) { return (ArenaTrie){ .arena = arena }; }
ENGINE_API ArenaTrieNode *arena_trienode_ensure(Arena *arena, ArenaTrieNode **root, Span key, const char *debug_type_name);
ENGINE_API void *arena_triestruct_ensure(Arena *arena, ArenaTrieHeader **root, size_t key_offset, size_t value_offset, Span key, size_t map_size, size_t map_align);
ENGINE_API void *arena_trie_ensure(Arena *arena, ArenaTrieNode **root, Span key, size_t size, size_t align, const char *debug_type_name);

#define arena_trie_wrap(buf, count) ((ArenaTrie){ .arena = &arena_wrap_count(buf, count) })
#define arena_trie_wrap_array(arr)                                                  \
	(ArenaTrie) {                                                                   \
		.arena = &(Arena){ .offset = 0, .capacity = sizeof((arr)), .base = (arr) }, \
		.root = NULL                                                                \
	}

#define arena_trienode_push(trie, key) (arena_trienode_ensure((trie)->arena, &(trie)->root, (key), NULL))
#define arena_trienode_find(trie, key) (arena_trienode_ensure(NULL, &(trie)->root, (key), NULL))
#define arena_trieset_push(trie, key) (arena_trienode_ensure((trie)->arena, &(trie)->root, (key), NULL))
#define arena_trieset_find(trie, key) (arena_trienode_ensure(NULL, &(trie)->root, (key), NULL))

#define arena_trie_push_count(trie, key, count, T) ((T *)arena_trie_ensure((trie)->arena, &(trie)->root, (key), sizeof(T) * count, alignof(T), #T))
#define arena_trie_push(trie, key, T) ((T *)arena_trie_ensure((trie)->arena, &(trie)->root, (key), sizeof(T), alignof(T), #T))
#define arena_trie_put(trie, key, T, ...)                                                               \
	do {                                                                                                \
		T _val = __VA_ARGS__;                                                                           \
		*(T *)arena_trie_ensure((trie)->arena, &(trie)->root, (key), sizeof(T), alignof(T), #T) = _val; \
	} while (0)
#define arena_trie_store(trie, T, ...)                                         \
	do {                                                                       \
		struct {                                                               \
			Span key;                                                          \
			T value;                                                           \
		} _values[] = __VA_ARGS__;                                             \
		for (uint32_t index = 0; index < countof(_values); ++index) {          \
			arena_trie_put(trie, _values[index].key, T, _values[index].value); \
		}                                                                      \
	} while (0)
#define arena_trie_find(trie, key, T) ((T *)arena_trie_ensure(NULL, &(trie)->root, (key), 0, 0, #T))

#define arena_triestruct_push(arena, root, k, T) ((T *)arena_triestruct_ensure((arena), (ArenaTrieHeader **)&(root), offsetof(T##Trie, key), offsetof(T##Trie, value), k, sizeof(T##Trie), alignof(T##Trie)))
#define arena_triestruct_put(arena, root, k, T, ...)                                                                                                                        \
	do {                                                                                                                                                                    \
		T _val = __VA_ARGS__;                                                                                                                                               \
		*(T *)arena_triestruct_ensure((arena), (ArenaTrieHeader **)&(root), offsetof(T##Trie, key), offsetof(T##Trie, value), k, sizeof(T##Trie), alignof(T##Trie)) = _val; \
	} while (0)
#define arena_triestruct_find(root, key_value, T) ((T *)arena_triestruct_ensure(NULL, (ArenaTrieHeader **)&(root), offsetof(T##Trie, key), offsetof(T##Trie, value), key_value, sizeof(T##Trie), alignof(T##Trie)))

typedef struct arena_list_node {
	struct arena_list_node *next;
} ArenaListNode;

typedef struct {
	Arena *arena;
	ArenaListNode *first;
} ArenaList;

void *arena_freelist_wrap(void *buffer, size_t stride, uint32_t capacity);
#define arena_freelist_wrap_array(array, T) (T *)arena_freelist_wrap((array), sizeof(T), countof(array))
#define arena_freelist_wrap_count(memory, count, T) (T *)arena_freelist_wrap((memory), sizeof(*memory), count)

void *arena_list_alloc(void **list);
void arena_list_free(void **list, void *node);

#define arena_list_pop(list, T) (T *)arena_list_alloc((void **)list)
#define arena_list_push(list, node) arena_list_free((void **)list, node)

typedef struct alignas(16) {
	uint32_t count, capacity;
} ArenaArrayHeader;

#define arena_array_make(arena, cap, T) (((ArenaArrayHeader *)arena_push(arena, sizeof(ArenaArrayHeader), 16, true))->capacity = cap, arena_push_count((arena), (cap), T))

#define arena_array_count(arr) ((arr) ? HEADER(arr, ArenaArrayHeader)->count : 0)
#define arena_array_capacity(arr) ((arr) ? HEADER(arr, ArenaArrayHeader)->capacity : 0)
#define arena_array_copy(arena, arr, T) \
	(arr) ? (T *)((ArenaArrayHeader *)arena_push_copy((arena), HEADER(arr, ArenaArrayHeader), sizeof(ArenaArrayHeader) + sizeof(T) * arena_array_count(arr), 16) + 1) : NULL

#define arena_array_push(arr) (&(arr)[HEADER(arr, ArenaArrayHeader)->count++])
#define arena_array_put(arr, T, ...)                          \
	do {                                                      \
		T _val = __VA_ARGS__;                                 \
		(arr)[HEADER(arr, ArenaArrayHeader)->count++] = _val; \
	} while (0)

ENGINE_API void *arena_array_ensure(Arena *arena, void *arr, size_t item_size);

#define arena_darray_push(arena, arr, T)                    \
	((arr) = arena_array_ensure((arena), (arr), sizeof(T)), \
		arena_array_push(arr))
#define arena_darray_put(arena, arr, T, ...)                   \
	do {                                                       \
		(arr) = arena_array_ensure((arena), (arr), sizeof(T)); \
		T _val = __VA_ARGS__;                                  \
		(arr)[HEADER(arr, ArenaArrayHeader)->count++] = _val;  \
	} while (0)
