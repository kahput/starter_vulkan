#pragma once

#include "common.h"
#include "core/arena.h"

typedef uint64_t UUID;

UUID identifier_generate(void);
ENGINE_API UUID identifier_create_from_u64(uint64_t uuid);

typedef struct {
	UUID id;
	uint32_t index;
} Handle;

Handle handle_create(uint32_t index);
ENGINE_API Handle handle_create_with_uuid(uint32_t index, UUID id);
bool handle_is_valid(Handle handle);

typedef struct index_recycler {
	uint32_t *free_indices;
	uint32_t free_count;
	uint32_t next_unused;
	uint32_t capacity;
} IndexRecycler;

void index_recycler_create(Arena *arena, IndexRecycler *recycler, uint32_t start_offset, uint32_t capacity);
uint32_t recycler_new_index(IndexRecycler *recycler);
void recycler_free_index(IndexRecycler *recycler, uint32_t index);
