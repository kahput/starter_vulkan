#include "hash_trie.h"
#include "core/arena.h"
#include "core/astring.h"

void *hash_trie_traverse_key(Arena *arena, HashTrieNode **root, String key, size_t node_size) {
	HashTrieNode **node = root;

	uint64_t hash = string_hash64(key);
	for (uint64_t hash_index = hash; *node; hash_index <<= 2) {
		if (string_equals(key, (*node)->key)) {
			return *node;
		}
		node = &(*node)->child[hash_index >> 62];
	}

	if (!arena)
		return NULL;

	*node = arena_push(arena, node_size, 1, true);
	(*node)->key = string_duplicate(arena, key);
	(*node)->hash = hash;

	return *node;
}

void *hash_trie_traverse_hash(Arena *arena, HashTrieNode **root, uint64_t hash, size_t node_size) {
	HashTrieNode **node = root;

	for (uint64_t hash_index = hash; *node; hash_index <<= 2) {
		if (hash == (*node)->hash) {
			return *node;
		}
		node = &(*node)->child[hash_index >> 62];
	}

	if (!arena)
		return NULL;

	*node = arena_push(arena, node_size, 1, true);
	(*node)->key = string_format(arena, S("%d"), hash);
	(*node)->hash = hash;

	return *node;
}
