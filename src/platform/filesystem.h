#pragma once

#include "common.h"

#include "allocators/arena.h"
#include "core/astring.h"

typedef enum {
	FILE_MODE_READ = 1 << 0,
	FILE_MODE_WRITE = 1 << 1,
} FileModeFlagBits;
typedef uint32_t FileModeFlags;

typedef struct {
	size_t size;
	uint8_t *content;
} FileContent;

typedef struct file_path_node {
	String path;
	struct file_path_node *next;
} FileNode;

bool filesystem_file_exists(String path);
FileContent filesystem_read(struct arena *arena, String path);

FileNode *filesystem_load_directory_files(Arena *arena, String directory_path, bool recursive);

uint64_t filesystem_last_modified(String path);
