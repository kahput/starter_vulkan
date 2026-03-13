#include "arena.h"

#include "common.h"

#include "core/debug.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>

thread_local Arena scratch_arenas[2] = { 0 };

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
	memset(arena->memory, 0, arena->offset);
	arena->offset = 0;
}

ArenaTemp arena_temp_begin(Arena *arena) {
	return (ArenaTemp){ .arena = arena, .position = arena_size(arena) };
}

void arena_temp_end(ArenaTemp temp) {
	arena_set(temp.arena, temp.position);
}

ArenaTemp arena_scratch_begin(Arena *conflict) {
	if (scratch_arenas[0].memory == NULL) {
		// TODO: Lower this back down
		scratch_arenas[0] = arena_make(MiB(256));
		scratch_arenas[1] = arena_make(MiB(256));
	}

	Arena *selected = conflict == &scratch_arenas[0] ? &scratch_arenas[1] : &scratch_arenas[0];
	return arena_temp_begin(selected);
}

ArenaTrieNode *arena_trienode_ensure(Arena *arena, ArenaTrieNode **root, uint64_t hash, const char *debug_type_name) {
	ArenaTrieNode **node = root;

	for (uint64_t hash_index = hash; *node; hash_index <<= 2) {
		if (hash == (*node)->hash) {
			ASSERT_FORMAT(
				debug_type_name && (*node)->debug_type_name
					? strcmp((*node)->debug_type_name, debug_type_name) == 0
					: 1,
				"ArenaTrie type mismatch: Stored: %s, Requested: %s",
				(*node)->debug_type_name, debug_type_name);

			return (*node);
		}
		node = &(*node)->children[hash_index >> 62];
	}

	if (arena == NULL)
		return NULL;

	*node = arena_push_struct(arena, ArenaTrieNode);
	(*node)->hash = hash;
	(*node)->debug_type_name = debug_type_name;

	return (*node);
}

void *arena_trie_ensure(Arena *arena, ArenaTrieNode **root, uint64_t hash, size_t size, size_t align, bool intrusive, const char *debug_type_name) {
	ArenaTrieNode *node = arena_trienode_ensure(arena, root, hash, debug_type_name);

	// Add
	if (arena && node && node->payload == NULL) {
		if (intrusive) {
			ASSERT(size > sizeof(ArenaTrieNode));
			arena_push(arena, size - sizeof(ArenaTrieNode), 1, true);
			node->payload = node;
		} else
			node->payload = arena_push(arena, size, align, true);
	}

	return node ? node->payload : NULL;
}

void *arena_freelist_wrap(void *array, size_t stride, uint32_t capacity) {
	for (uint32_t index = 0; index < capacity; ++index) {
		ArenaListNode *element = (ArenaListNode *)((uint8_t *)array + stride * index);
		element->next = (ArenaListNode *)((uint8_t *)element + stride);

		if (index + 1 == capacity)
			element->next = NULL;
	}

	return array;
}

void *arena_list_alloc(void **first_free) {
	if (first_free == NULL || *first_free == NULL)
		return NULL;

	ArenaListNode *element = (ArenaListNode *)*first_free;
	*first_free = element->next;

	return element;
}

void arena_list_free(void **first_free, void *slot) {
	if (first_free == NULL || *first_free == NULL)
		return;

	ArenaListNode *head = *first_free;
	ArenaListNode *element = slot;

	element->next = head;
	*first_free = element;
}

/* void *arena_list_push(Arena *arena, ArenaListNode **first, size_t size, size_t align) { */
/* 	if (arena == NULL) */
/* 		return NULL; */

/* 	ArenaListNode *node = arena_push(arena, sizeof(ArenaListNode), align, true); */

/* 	if (*first) { */
/* 		node->next = *first; */
/* 		*first = node; */
/* 	} else */
/* 		*first = node; */

/* 	return arena_push(arena, size, align, true); */
/* } */

/* void *arena_list_pop(ArenaListNode **first) { */
/* 	if (first == NULL || *first == NULL) */
/* 		return NULL; */

/* 	ArenaListNode *element = (ArenaListNode *)*first; */
/* 	*first = element->next; */

/* 	return element + 1; */
/* } */
