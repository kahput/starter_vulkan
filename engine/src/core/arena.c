#include "arena.h"

#include "common.h"

#include "core/debug.h"
#include "core/logger.h"

#include <stdlib.h>
#include <string.h>

Arena scratch_arenas[2] = { 0 };

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
		memory_zero((void *)aligned, size);

	arena->offset += padding + size;
	return (void *)aligned;
}

void *arena_push_copy(Arena *arena, void *src, size_t size, size_t align) {
	void *dst = arena_push(arena, size, align, false);
	memcpy(dst, src, size);
	return dst;
}

void arena_pop(Arena *arena, size_t size) {
	arena->offset = size > arena->offset ? 0 : arena->offset - size;
}

void arena_rewind(Arena *arena, size_t position) {
	arena->offset = position > arena->capacity ? arena->capacity : position;
}

size_t arena_mark(Arena *arena) {
	return arena->offset;
}

void arena_reset(Arena *arena) {
	memory_zero(arena->memory, arena->offset);
	arena->offset = 0;
}

ArenaTemp arena_temp_begin(Arena *arena) {
	return (ArenaTemp){ .arena = arena, .position = arena_mark(arena) };
}

void arena_temp_end(ArenaTemp temp) {
	arena_rewind(temp.arena, temp.position);
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

ArenaTrieNode *arena_trienode_ensure(Arena *arena, ArenaTrieNode **root, Span key, const char *debug_type_name) {
	ArenaTrieNode **node = root;

	for (uint64_t hash_index = hash64(key.ptr, key.length); *node; hash_index <<= 2) {
		Span node_key = span_make((uint8_t *)(*node) + sizeof(ArenaTrieNode), (*node)->key_size);
		if (span_equal(node_key, key)) {
			ASSERT_FORMAT(
				debug_type_name && (*node)->debug_type_name
					? strcmp((*node)->debug_type_name, debug_type_name) == 0
					: 1,
				"ArenaTrie type mismatch: Stored: %s, Requested: %s",
				(*node)->debug_type_name, debug_type_name);

			return (*node);
		}
		node = &((*node)->children[hash_index >> 62]);
	}

	if (arena == NULL)
		return NULL;

	*node = arena_push(arena, sizeof(ArenaTrieNode) + key.length, alignof(ArenaTrieNode), true);
	(*node)->key_size = key.length;
	(*node)->debug_type_name = debug_type_name;
	memory_copy((uint8_t *)(*node) + sizeof(ArenaTrieNode), key.ptr, (*node)->key_size);

	return (*node);
}

void *arena_triestruct_ensure(Arena *arena, ArenaTrieHeader **root, size_t key_offset, size_t value_offset, Span key, size_t map_size, size_t map_align) {
	ArenaTrieHeader **node = root;

	for (uint64_t hash = hash64(key.ptr, key.length); *node; hash <<= 2) {
		void *node_key = (uint8_t *)(*node) + key_offset;
		if (memory_equals(node_key, key.ptr, key.length)) {
			return (uint8_t *)(*node) + value_offset;
		}

		node = &(*node)->children[hash >> 62];
	}

	if (arena == NULL)
		return NULL;

	ASSERT(map_size >= sizeof(ArenaTrieNode));

	(*node) = arena_push(arena, map_size, 16, true);
	memory_copy((uint8_t *)(*node) + key_offset, key.ptr, key.length);

	return (uint8_t *)(*node) + value_offset;
}

void *arena_trie_ensure(Arena *arena, ArenaTrieNode **root, Span key, size_t size, size_t align, const char *debug_type_name) {
	ArenaTrieNode *node = arena_trienode_ensure(arena, root, key, debug_type_name);
	if (arena && node && node->payload == NULL)
		node->payload = arena_push(arena, size, align, true);

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

#define INITIAL_DARRAY_CAPACITY 64
void *arena_array_ensure(Arena *arena, void *arr, size_t item_size) {
	uint32_t capacity = 0;

	if (arr) {
		ArenaArrayHeader *header = HEADER(arr, ArenaArrayHeader);
		if (header->count < header->capacity)
			return arr;
		capacity = header->capacity;

		if ((uint8_t *)arena->memory + arena->offset != (uint8_t *)arr + capacity * item_size) {
			void *copy = arena_push_copy(arena, HEADER(arr, ArenaArrayHeader), sizeof(ArenaArrayHeader) + capacity * item_size, 16);
			arr = (ArenaArrayHeader *)copy + 1;
		}
	} else {
		ArenaArrayHeader *header = arena_push(arena, sizeof(ArenaArrayHeader), 16, true);
		arr = header + 1;
	}

	uint32_t extend = capacity ? capacity : INITIAL_DARRAY_CAPACITY;
	arena_push(arena, extend * item_size, 1, true);
	HEADER(arr, ArenaArrayHeader)->capacity += extend;

	return arr;
}
