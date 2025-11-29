#pragma once

#include "common.h"

typedef uint64_t UUID;
#define INVALID_UUID 0

UUID identifier_create(void);
UUID identifier_create_from_u64(uint64_t uuid);

typedef struct {
	uint32_t packed;
	UUID uuid;
} Handle;

#define INVALID_INDEX UINT32_MAX
#define INVALID_HANDLE (Handle){ INVALID_INDEX, 0 }

Handle handle_create(uint32_t index);
Handle handle_create_with_uuid(uint32_t index, UUID id);

bool handle_valid(Handle handle);

uint32_t handle_index(Handle handle);
uint8_t handle_generation(Handle handle);

bool handle_increment(Handle *handle);
