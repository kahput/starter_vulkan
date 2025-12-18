#pragma once

#include "common.h"

typedef uint64_t UUID;
#define INVALID_UUID UINT64_MAX

UUID identifier_create(void);
UUID identifier_create_from_u64(uint64_t uuid);

typedef struct {
	uint32_t packed;
} Handle;

#define INVALID_INDEX UINT32_MAX
#define INVALID_HANDLE ((Handle){ INVALID_INDEX })

Handle handle_create(uint32_t index);
bool handle_valid(Handle handle);

uint32_t handle_index(Handle handle);
uint8_t handle_generation(Handle handle);

bool handle_increment(Handle *handle);
