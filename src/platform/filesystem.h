#pragma once

#include "common.h"
#include "core/astring.h"

typedef struct {
	size_t size;
	uint8_t *content;
} File;

typedef enum {
	FILE_MODE_READ = 1 << 0,
	FILE_MODE_WRITE = 1 << 1,
} FileModeFlagBits;
typedef uint32_t FileModeFlags;

bool filesystem_file_exists(String path);
File filesystem_read(struct arena *arena, String path);
