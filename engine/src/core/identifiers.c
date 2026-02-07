#include "identifiers.h"
#include <stdlib.h>

#include "common.h"
#include "core/debug.h"
#include "core/logger.h"

// TODO: Pass platform random value instead
#include <sys/random.h>

#define HANDLE_INDEX_BITS 24
#define HANDLE_GENERATION_BITS 8
#define HANDLE_INDEX_MASK ((1 << HANDLE_INDEX_BITS) - 1)
#define HANDLE_GENERATION_MASK ((1 << HANDLE_GENERATION_BITS) - 1)

UUID identifier_generate(void) {
	uint64_t random_value = 0;
	while (random_value == 0)
		if (getrandom(&random_value, sizeof(random_value), GRND_RANDOM) != sizeof(random_value))
			LOG_WARN("Identifier: Random number failed");
	return random_value;
}

UUID identifier_create_from_u64(uint64_t uuid) {
	return (UUID)uuid;
}

Handle handle_create(uint32_t index) {
	return (Handle){ .id = identifier_generate(), .index = index };
}

Handle handle_create_with_uuid(uint32_t index, UUID id) {
	return (Handle){ .id = id, .index = index };
}

bool handle_is_valid(Handle handle) {
	return handle.index != 0 && handle.id != 0;
}

void index_recycler_create(Arena *arena, IndexRecycler *recycler, uint32_t start_offset, uint32_t capacity) {
	recycler->free_indices = arena_push_array_zero(arena, uint32_t, capacity);
	recycler->free_count = recycler->next_unused = 0;
	recycler->capacity = capacity;
	recycler->next_unused = start_offset;
}

uint32_t recycler_new_index(IndexRecycler *recycler) {
	if (recycler->free_count > 0)
		return recycler->free_indices[--recycler->free_count];

	ASSERT(recycler->next_unused < recycler->capacity);
	return recycler->next_unused++;
}

void recycler_free_index(IndexRecycler *handler, uint32_t handle) {
	handler->free_indices[handler->free_count++] = handle;
}
