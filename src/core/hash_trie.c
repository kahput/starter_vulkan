#include "hash_trie.h"
#include "allocators/arena.h"

void *hash_trie_traverse_(Arena *arena, HashTrieNode **root, String key, size_t node_size) {
	HashTrieNode **node = root;

	for (uint64_t hash = string_hash64(key); *node; hash <<= 2) {
		if (string_equals(key, (*node)->key)) {
			return *node;
		}
		node = &(*node)->child[hash >> 62];
	}

	if (!arena)
		return NULL;

	*node = arena_push_zero(arena, node_size);
	(*node)->key = string_copy(arena, key);

	return *root;
}
