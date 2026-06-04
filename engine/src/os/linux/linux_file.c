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
String os__concat_cwd(Arena *arena, String path);
uint64_t os__open_file_cwd(String path, int32_t flags, int32_t access);
DIR *os__open_dir_cwd(String path);

OS_File os_file_open(String filepath, OS_FileMode mode) {
	OS_File result = os__open_file_cwd(filepath, os__mode_to_flags(mode), 0666);
	if (result == OS_FILE_INVALID)
		LOG_WARN("%s", strerror(errno));

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

bool os_file_exists(String filepath) {
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

bool os_file_copy(String src, String dst) {
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

String os_file_read_entire(Arena *arena, String path) {
	String result = { 0 };

	OS_File handle = os_file_open(path, OS_FILE_MODE_READ);
	if (os_file_valid(handle)) {
		uint64_t size = os_file_size(handle);
		uint8_t *buffer = arena_push_size(arena, size + 1);
		os_file_read(handle, buffer, size + 1);
		buffer[size] = '\0';

		result.text = (char *)buffer;
		result.length = size;

		os_file_close(handle);
	}

	return result;
}

void os_file_write_entire(String path, const void *buffer, uint64_t size) {
	OS_File handle = os_file_open(path, OS_FILE_MODE_WRITE);
	if (os_file_valid(handle)) {
		os_file_write(handle, buffer, size);
		os_file_close(handle);
	}
}

uint64_t os_file_last_modified(String filefilepath) {
	struct stat attrib;
	if (stat(filefilepath.text, &attrib) == 0)
		return (uint64_t)attrib.st_mtime;

	return 0;
}

String os_current_directory(Arena *arena) {
	return os__concat_cwd(arena, str_lit(""));
}

bool os_directory_exists(String path) {
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

bool os_directory_make(String path) {
	bool result = false;
	if (os_directory_exists(path) == false) {
		ArenaTemp scratch = arena_scratch_begin(NULL);
		int32_t result = mkdir(os__concat_cwd(scratch.arena, path).text, 0755);
		if (result == -1)
			LOG_WARN("os_directory_make - %s", strerror(errno));
		arena_scratch_end(scratch);

		result = true;
	}

	return result;
}

bool os_directory_delete(String path) {
	bool result = false;
	if (os_directory_exists(path) == true) {
		ArenaTemp scratch = arena_scratch_begin(NULL);
		int32_t result = rmdir(os__concat_cwd(scratch.arena, path).text);
		if (result == -1)
			LOG_WARN("os_directory_make - %s", strerror(errno));
		arena_scratch_end(scratch);

		result = true;
	}

	return result;
}

String os__concat_cwd(Arena *arena, String path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	String result = { 0 };

	uint32_t initial_size = 256;
	uint8_t *buffer = arena_push_size(scratch.arena, initial_size);
	while (getcwd((void *)buffer, initial_size) == NULL) {
		initial_size += 256;
		arena_push_size(scratch.arena, 256);
	}

	result = stringpath_join(arena, string_wrap((char *)buffer), path);
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

uint64_t os__open_file_cwd(String path, int32_t flags, int32_t access) {
	uint64_t result = OS_FILE_INVALID;
	ArenaTemp scratch = arena_scratch_begin(NULL);
	int32_t open_result = open(os__concat_cwd(scratch.arena, path).text, flags, access);
	arena_scratch_end(scratch);

	if (open_result != -1)
		result = open_result;

	return result;
}

DIR *os__open_dir_cwd(String path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	DIR *result = opendir(os__concat_cwd(scratch.arena, path).text);
	arena_scratch_end(scratch);

	return result;
}
