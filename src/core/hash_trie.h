#pragma once

#include "common.h"
#include "string_view.h"

typedef struct hash_trie_node {
	struct hash_trie_node *child[4];
	StringView key;
} HashTrieNode;

#define hash_trie_insert(arena, root, key, type) ((void)sizeof(char[1 - 2*!!offsetof(type, node)]),\
	(type *)hash_trie_traverse_((HashTrieNode **)(root), (key), (arena), sizeof(type))

#define hash_trie_lookup(root, key, type) ((void)sizeof(char[1 - 2*!!offsetof(type, node)]),\
	(type *)hash_trie_traverse_((HashTrieNode **)(root), (key), NULL, 0)

void *hash_trie_traverse_(struct arena *arena, HashTrieNode **root, StringView key,
	size_t node_size);
