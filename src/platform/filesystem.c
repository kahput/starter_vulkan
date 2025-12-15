#include "filesystem.h"
#include "allocators/arena.h"
#include "core/logger.h"
#include <errno.h>
#include <stdio.h>

bool filesystem_file_exists(String path) {
	FILE *file = fopen((const char *)path.data, "r");
	if (file) {
		fclose(file);
		return true;
	}
	return false;
}

File filesystem_read(Arena *arena, String path) {
	FILE *file = fopen((const char *)path.data, "rb");
	if (file == NULL) {
		LOG_ERROR("Failed to read file %s: %s", path, strerror(errno));
		return (File){ 0 };
	}

	fseek(file, 0L, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0L, SEEK_SET);

	uint8_t *byte_content = arena_push_array_zero(arena, uint8_t, size);
	fread(byte_content, sizeof(*byte_content), size, file);

	fclose(file);

	return (File){ .size = size, .content = byte_content };
}
