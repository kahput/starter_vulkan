#pragma once

#include "common.h"

#define HT_MAX_KEY_SIZE 255
#define HT_CAPACITY 509

struct arena;
typedef struct _hash_table HashTable;

HashTable *ht_create(struct arena *arena, size_t type_size, size_t alignment);

void ht_insert(HashTable *ht, const char *key, const void *value);
void *ht_search(HashTable *ht, const char *key);
void ht_remove(HashTable *ht, const char *key);

uint32_t ht_length(HashTable *ht);
