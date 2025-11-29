#include "identifiers.h"
#include <stdlib.h>

#include "core/logger.h"

// TODO: Pass platform random value instead
#include <sys/random.h>

#define HANDLE_INDEX_BITS 24
#define HANDLE_GENERATION_BITS 8
#define HANDLE_INDEX_MASK ((1 << HANDLE_INDEX_BITS) - 1)
#define HANDLE_GENERATION_MASK ((1 << HANDLE_GENERATION_BITS) - 1)

UUID identifier_create(void) {
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
	return (Handle){ .packed = index, .uuid = identifier_create() };
}

bool handle_valid(Handle handle) {
	return handle.packed != UINT32_MAX && handle.uuid != INVALID_UUID;
}

uint32_t handle_index(Handle handle) {
	return handle.packed & HANDLE_INDEX_MASK;
}

uint8_t handle_generation(Handle handle) {
	return (handle.packed >> HANDLE_INDEX_BITS) & HANDLE_GENERATION_MASK;
}

bool handle_increment(Handle *handle) {
	uint8_t generation = handle_generation(*handle) + 1;

	if (generation >= HANDLE_GENERATION_MASK) {
		LOG_WARN("Identifier: Handle generation wrapping");
		handle->packed = handle_index(*handle) | (0 << HANDLE_INDEX_BITS);
		return false;
	}

	handle->packed = handle_index(*handle) | (generation << HANDLE_INDEX_BITS);
	return true;
}
