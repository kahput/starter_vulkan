#pragma once

#include "common.h"

#include "core/arena.h"
#include "core/astring.h"

typedef enum {
	FILE_MODE_READ = 1 << 0,
	FILE_MODE_WRITE = 1 << 1,
} FileModeFlagBits;
typedef uint32_t FileModeFlags;

typedef struct file_path_node {
	String path;
	struct file_path_node *next;
} FileNode;

bool filesystem_file_exists(String path);
FileContent filesystem_read(struct arena *arena, String path);

bool filesystem_file_copy(String from, String to);
StringList filesystem_list_files(Arena *arena, String directory_path, bool recursive);

uint64_t filesystem_last_modified(String path);
