#include "hash_table.h"

#include "core/arena.h"
#include "core/logger.h"

#include <math.h>
#include <string.h>

size_t strnlen(const char *s, size_t maxlen) {
	size_t i;
	for (i = 0; i < maxlen && s[i]; i++)
		;
	return i;
}
#define HT_PRIME_1 157
#define HT_PRIME_2 151

struct _hash_table {
	size_t type_size, item_size;
	uint32_t count, capacity;
	void *items;
};

static void *HT_TOMBSTONE = (void *)-1;

static int ht_hash(const char *s, const int a, const int m);
static int ht_get_hash(const char *s, const int num_buckets, const int attempt);

HashTable *ht_create(Arena *arena, size_t type_size, size_t alignment) {
	if (arena == NULL || type_size <= 0) {
		LOG_ERROR("ht_create(): Invalid parameters ");
		return NULL;
	}

	HashTable *ht = arena_push_struct(arena, HashTable);
	*ht = (HashTable){
		.capacity = HT_CAPACITY,
		.count = 0,
		.items = NULL,
		.type_size = type_size,
	};

	ht->item_size = HT_MAX_KEY_SIZE + ht->type_size;

	ht->items = arena_push(arena, ht->item_size * ht->capacity, alignment, true);
	return ht;
}

void ht_insert(HashTable *ht, const char *key, const void *value) {
	if (ht == NULL || key == NULL || value == NULL) {
		LOG_ERROR("ht_insert(): Invalid parameters");
		return;
	}
	if (ht->count == ht->capacity) {
		LOG_WARN("ht_insert(): HashTable full, key ignored");
		return;
	}

	uint32_t str_length = (uint32_t)strnlen(key, HT_MAX_KEY_SIZE);
	if (str_length == HT_MAX_KEY_SIZE)
		LOG_WARN("ht_insert(): Key missing null-terminator within HT_MAX_KEY_SIZE = %i", HT_MAX_KEY_SIZE);

	char search_key[HT_MAX_KEY_SIZE];
	memcpy(search_key, key, str_length);
	search_key[str_length] = '\0';

	int32_t index = 0;
	char *items = ht->items;
	for (uint32_t i = 0; i < ht->capacity; i++) {
		index = ht_get_hash(search_key, ht->capacity, i);
		char *current_item = items + (ht->item_size * index);
		if (current_item[0] == '\0' || memcmp(current_item, &HT_TOMBSTONE, sizeof(void *)) == 0) {
			ht->count++;
			break;
		}
		if (strcmp(current_item, search_key) == 0)
			break;
	}

	LOG_INFO("Item being placed at index %i", index);
	memcpy(items + (ht->item_size * index), search_key, sizeof(search_key));
	memcpy((items + (ht->item_size * index)) + HT_MAX_KEY_SIZE, value, ht->type_size);
}

void *ht_search(HashTable *ht, const char *key) {
	if (ht == NULL || key == NULL) {
		LOG_ERROR("ht_search(): Invalid parameters");
		return NULL;
	}
	uint32_t str_length = strnlen(key, HT_MAX_KEY_SIZE);
	if (str_length == HT_MAX_KEY_SIZE)
		LOG_WARN("ht_search(): Key missing null-terminator within HT_MAX_KEY_SIZE = %i", HT_MAX_KEY_SIZE);

	char search_key[HT_MAX_KEY_SIZE];
	memcpy(search_key, key, str_length);
	search_key[str_length] = '\0';

	int32_t index = 0;
	char *items = ht->items;
	for (uint32_t i = 0; i < ht->capacity; i++) {
		index = ht_get_hash(search_key, ht->capacity, i);
		char *current_item = items + (ht->item_size * index);
		if (memcmp(current_item, &HT_TOMBSTONE, sizeof(void *)) == 0)
			continue;
		if (current_item[0] == '\0')
			return NULL;

		if (strcmp(current_item, search_key) == 0) {
			return current_item + HT_MAX_KEY_SIZE;
		}
	}
	return NULL;
}
void ht_remove(HashTable *ht, const char *key) {
	if (ht == NULL || key == NULL) {
		LOG_ERROR("ht_remove(): Invalid parameters");
		return;
	}
	uint32_t str_length;
	if ((str_length = strnlen(key, HT_MAX_KEY_SIZE)) == HT_MAX_KEY_SIZE)
		LOG_WARN("ht_insert(): Key missing null-terminator within HT_MAX_KEY_SIZE = %i", HT_MAX_KEY_SIZE);

	char search_key[HT_MAX_KEY_SIZE];
	memcpy(search_key, key, str_length);
	search_key[str_length] = '\0';

	int32_t index = 0;
	char *items = ht->items;
	for (uint32_t i = 0; i < ht->capacity; i++) {
		index = ht_get_hash(search_key, ht->capacity, i);
		char *current_item = items + (ht->item_size * index);
		if (current_item[0] == '\0')
			return;

		if (strcmp(current_item, search_key) == 0) {
			// NOTE: Might be worth to set it all to 0 instead
			// memset(current_item, 0, ht->item_size);
			memcpy(current_item, &HT_TOMBSTONE, sizeof(void *));
			ht->count--;
			return;
		}
	}
}

// hash_table.c
int ht_hash(const char *s, const int a, const int m) {
	long hash = 0;
	const int len_s = strlen(s);
	for (int i = 0; i < len_s; i++) {
		hash += (long)pow(a, len_s - (i + 1)) * s[i];
		hash = hash % m;
	}
	return (int)hash;
}

int ht_get_hash(const char *s, const int num_buckets, const int attempt) {
	const int hash_a = ht_hash(s, HT_PRIME_1, num_buckets);
	const int hash_b = ht_hash(s, HT_PRIME_2, num_buckets);
	return (hash_a + (attempt * (hash_b + 1))) % num_buckets;
}

uint32_t ht_length(HashTable *ht) {
	return ht->count;
}
