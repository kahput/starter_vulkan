#pragma once

#include "common.h"
#include "core/astring.h"

typedef struct hash_trie_node {
	struct hash_trie_node *child[4];
	String key;
	uint64_t hash;
} HashTrieNode;

#define hash_trie_insert(arena, root, key, type) ((type *)hash_trie_traverse_key((arena), (HashTrieNode **)(root), (key), sizeof(type)))
#define hash_trie_lookup(root, key, type) ((type *)hash_trie_traverse_key(NULL, (HashTrieNode **)(root), (key), 0))

#define hash_trie_lookup_hash(root, hash, type) ((type *)hash_trie_traverse_hash(NULL, (HashTrieNode **)(root), (hash), 0))
#define hash_trie_insert_hash(arena, root, hash, type) ((type *)hash_trie_traverse_hash((arena), (HashTrieNode **)(root), (hash), sizeof(type)))

void *hash_trie_traverse_key(struct arena *arena, HashTrieNode **root, String key, size_t node_size);
void *hash_trie_traverse_hash(Arena *arena, HashTrieNode **root, uint64_t hash, size_t node_size);
