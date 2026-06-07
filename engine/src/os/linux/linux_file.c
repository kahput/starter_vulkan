#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

int32_t os__mode_to_flags(OS_FileMode mode);
String8 os__concat_cwd(Arena *arena, String8 path);
uint64_t os__open_file_cwd(String8 path, int32_t flags, int32_t access);
DIR *os__open_dir_cwd(String8 path);

OS_File os_file_open(String8 filepath, OS_FileMode mode) {
	OS_File result = os__open_file_cwd(filepath, os__mode_to_flags(mode), 0666);
	if (result == OS_FILE_INVALID)
		LOG_WARN("failed to read '%.*s' - %s", filepath.length, filepath.text, strerror(errno));

	return result;
}

uint64_t os_file_size(OS_File handle) {
	uint64_t result = 0;
	if (os_file_valid(handle)) {
		uint64_t original_offset = lseek(handle, 0, SEEK_CUR);
		result = lseek(handle, 0, SEEK_END);
		lseek(handle, original_offset, SEEK_SET);
	}

	return result;
}

void os_file_close(OS_File file) {
	if (os_file_valid(file) == false)
		return;

	// EBADF - fd isn't a valid open file descriptor.
	if (close(file) == -1)
		LOG_WARN("%s", strerror(errno));
}

bool os_file_exists(String8 filepath) {
	bool result = false;
	uint64_t fd = os__open_file_cwd(filepath, O_RDONLY, 0);

	// ENOENT - O_CREAT is not set and the named file does not exist.
	if (fd) {
		result = true;
		fd = close(fd);
	} else if (errno != ENOENT)
		LOG_WARN("os_file_exists - %s", strerror(errno));

	return result;
}

uint64_t os_file_read(OS_File file, void *buffer, uint64_t size) {
	if (buffer == NULL || size == 0)
		return 0;
	uint8_t *running_offset = buffer;

	int64_t read_bytes = 0;
	while ((read_bytes = read(file, running_offset, size)) > 0) {
		running_offset += read_bytes;
		size -= read_bytes;
	}

	return running_offset - (uint8_t *)buffer;
}

uint64_t os_file_write(OS_File file, const void *buffer, uint64_t size) {
	uint8_t *running_offset = (uint8_t *)buffer;
	int64_t written_bytes = 0;

	while ((written_bytes = write(file, running_offset, size)) > 0) {
		size -= written_bytes;
		running_offset += written_bytes;
	}

	if (written_bytes == -1)
		LOG_WARN("os_file_write - %s", strerror(errno));

	return running_offset - (uint8_t *)buffer;
}

bool os_file_copy(String8 src, String8 dst) {
	OS_File input = os_file_open(src, OS_FILE_MODE_READ);
	if (os_file_valid(input) == false)
		return false;

	OS_File output = os_file_open(src, OS_FILE_MODE_READWRITE);
	if (os_file_valid(output) == false) {
		os_file_close(input);
		return false;
	}

	char buffer[4096];
	int64_t read_write_bytes;
	while ((read_write_bytes = read(input, buffer, sizeof(buffer))) > 0)
		write(output, buffer, read_write_bytes);

	os_file_close(input);
	os_file_close(output);

	return true;
}

String8 os_file_read_entire(Arena *arena, String8 path) {
	String8 result = { 0 };

	OS_File handle = os_file_open(path, OS_FILE_MODE_READ);
	if (os_file_valid(handle)) {
		uint64_t size = os_file_size(handle);
		uint8_t *buffer = arena_push(arena, size + 1, 8, true);
		os_file_read(handle, buffer, size);
		buffer[size] = '\0';

		result.text = buffer;
		result.length = size;

		os_file_close(handle);
	}

	return result;
}

void os_file_write_entire(String8 path, const void *buffer, uint64_t size) {
	OS_File handle = os_file_open(path, OS_FILE_MODE_WRITE);
	if (os_file_valid(handle)) {
		os_file_write(handle, buffer, size);
		os_file_close(handle);
	}
}

uint64_t os_file_last_modified(String8 path) {
	struct stat attrib;
	if (stat((char *)path.text, &attrib) == 0)
		return (uint64_t)attrib.st_mtime;

	return 0;
}

String8 os_current_directory(Arena *arena) {
	return os__concat_cwd(arena, str_lit(""));
}

bool os_directory_exists(String8 path) {
	bool result = false;
	DIR *dir = os__open_dir_cwd(path);

	// ENOENT - Directory does not exist, or name is an empty string.
	if (dir) {
		result = true;
		closedir(dir);
	} else if (errno != ENOENT)
		LOG_WARN("os_directory_exists - %s", strerror(errno));

	return result;
}

bool os_directory_make(String8 path) {
	bool result = false;
	if (os_directory_exists(path) == false) {
		ArenaTemp scratch = arena_scratch_begin(NULL);
		int32_t result = mkdir((char *)os__concat_cwd(scratch.arena, path).text, 0755);
		if (result == -1)
			LOG_WARN("os_directory_make - %s", strerror(errno));
		arena_scratch_end(scratch);

		result = true;
	}

	return result;
}

bool os_directory_delete(String8 path) {
	bool result = false;
	if (os_directory_exists(path) == true) {
		ArenaTemp scratch = arena_scratch_begin(NULL);
		int32_t result = rmdir((char *)os__concat_cwd(scratch.arena, path).text);
		if (result == -1)
			LOG_WARN("os_directory_make - %s", strerror(errno));
		arena_scratch_end(scratch);

		result = true;
	}

	return result;
}

String8 os__concat_cwd(Arena *arena, String8 path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	String8 result = { 0 };

	uint32_t initial_size = 256;
	uint8_t *buffer = arena_push(scratch.arena, initial_size, 8, true);
	while (getcwd((void *)buffer, initial_size) == NULL) {
		initial_size += 256;
		arena_push(scratch.arena, 256, 1, true);
	}

	result = str8_path_join(arena, str8_wrap((char *)buffer), path);
	arena_scratch_end(scratch);

	return result;
}

int32_t os__mode_to_flags(OS_FileMode mode) {
	int32_t result = 0;
	switch (mode) {
		case OS_FILE_MODE_READ:
			result = O_RDONLY;
			break;
		case OS_FILE_MODE_WRITE:
			result = O_WRONLY | O_CREAT | O_TRUNC;
			break;
		case OS_FILE_MODE_READWRITE:
			result = O_RDWR | O_CREAT | O_TRUNC;
			break;
	}

	return result;
}

uint64_t os__open_file_cwd(String8 path, int32_t flags, int32_t access) {
	uint64_t result = OS_FILE_INVALID;
	ArenaTemp scratch = arena_scratch_begin(NULL);
	int32_t open_result = open((char *)os__concat_cwd(scratch.arena, path).text, flags, access);
	arena_scratch_end(scratch);

	if (open_result != -1)
		result = open_result;

	return result;
}

DIR *os__open_dir_cwd(String8 path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	DIR *result = opendir((char *)os__concat_cwd(scratch.arena, path).text);
	arena_scratch_end(scratch);

	return result;
}
