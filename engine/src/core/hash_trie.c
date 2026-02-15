#include "hash_trie.h"
#include "core/arena.h"

void *hash_trie_traverse(Arena *arena, HashTrieNode **root, size_t stride, uint64_t hash) {
	HashTrieNode **node = root;

	for (uint64_t hash_index = hash; *node; hash_index <<= 2) {
		if (hash == (*node)->hash) {
			return *node;
		}
		node = &(*node)->child[hash_index >> 62];
	}

	if (!arena)
		return NULL;

	*node = arena_push(arena, stride, 1, true);
	(*node)->hash = hash;

	return *node;
}
