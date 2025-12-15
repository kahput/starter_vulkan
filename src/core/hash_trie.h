#pragma once

#include "astring.h"
#include "common.h"

typedef struct hash_trie_node {
	struct hash_trie_node *child[4];
	String key;
} HashTrieNode;

#define hash_trie_insert(arena, root, key, type) \
	(type *)hash_trie_traverse_((arena), (HashTrieNode **)(root), (key), sizeof(type))

#define hash_trie_lookup(root, key, type) \
	(type *)hash_trie_traverse_(NULL, (HashTrieNode **)(root), (key), 0)

void *hash_trie_traverse_(struct arena *arena, HashTrieNode **root, String key,
	size_t node_size);
