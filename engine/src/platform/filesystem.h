#pragma once

#include "common.h"

#include "core/arena.h"
#include "core/strings.h"

typedef enum {
	FILE_MODE_READ,
	FILE_MODE_WRITE,
    FILE_MODE_WRITE_BINARY,
} FileMode;

ENGINE_API bool file_exists(String path);
ENGINE_API Buffer filesystem_read(struct arena *arena, String path);

typedef struct {
	void *handle;
} File;

// NOTE: Maybe?
/* ENGINE_API void file_write_begin(String path); */
/* ENGINE_API void file_write_end(void); */

ENGINE_API File filesystem_open(String path, FileMode mode);
ENGINE_API size_t file_write(File *file, size_t element_size, uint32_t element_count, void *data);
ENGINE_API void file_close(File *file);

ENGINE_API void filesystem_make_directory(String directory);

#define file_write_struct(file, T, ...)            \
	do {                                           \
		T _value = __VA_ARGS__;                    \
		file_write((file), sizeof(T), 1, &_value); \
	} while (0)
#define file_write_count(file, count, data) file_write(file, sizeof(*data), count, data)

bool filesystem_file_copy(String from, String to);
ENGINE_API StringList filesystem_directory_files(Arena *arena, String directory_path, bool recursive);

uint64_t filesystem_last_modified(String path);
