#pragma once

#include "common.h"
#include "arena.h"

typedef struct hash_trie_node {
	struct hash_trie_node *child[4];
	uint64_t hash;
} HashTrieNode;

#define hash_trie_insert(arena, root, key, type) ((type *)hash_trie_traverse((arena), (HashTrieNode **)(root), sizeof(type), (key)))
#define hash_trie_lookup(root, key, type) ((type *)hash_trie_traverse(NULL, (HashTrieNode **)(root), 0, (key)))

void *hash_trie_traverse(Arena *arena, HashTrieNode **root, size_t stride, uint64_t hash);
