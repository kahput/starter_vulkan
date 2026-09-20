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
string8 os__concat_cwd(Arena *arena, string8 path);
uint64_t os__open_file(string8 path, int32_t flags, int32_t access);
uint64_t os__open_fulllpath(string8 path, int32_t flags, int32_t access);
DIR *os__open_dir(string8 path);

OS_File os_file_open(string8 filepath, OS_FileMode mode) {
	int32_t flags = os__mode_to_flags(mode), access = 0666;
	OS_File result = os__open_fulllpath(filepath, flags, access);
	if (result == OS_INVALID_FILE)
		result = os__open_file(filepath, flags, access);

	if (result == OS_INVALID_FILE) {
		LOG_WARN("failed to read '%.*s' - %s", filepath.length, filepath.bytes, strerror(errno));
	}

	return result;
}

OS_File os_file_open_async(string8 path, OS_FileMode mode) {
	OS_File result = os__open_fulllpath(path, os__mode_to_flags(mode), 0666);
	if (os_file_valid(result) == false) {
		LOG_WARN("failed to read '%.*s' - %s", arg8(path), strerror(errno));
	}

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
	if (close(file) == -1) {
		LOG_WARN("%s", strerror(errno));
	}
}

bool os_file_exists(string8 filepath) {
	bool result = false;
	uint64_t fd = os__open_fulllpath(filepath, O_RDONLY, 0);

	// ENOENT - O_CREAT is not set and the named file does not exist.
	if (fd) {
		result = true;
		fd = close(fd);
	} else if (errno != ENOENT) {
		LOG_WARN("os_file_exists - %s", strerror(errno));
	}

	return result;
}

bool os_file_delete(string8 path) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool relative = path.bytes[0] != '/';
	string8 full_path = relative ? os__concat_cwd(scratch.arena, path) : path;

	int32_t result = remove((char *)full_path.bytes);
	if (result == -1) {
		LOG_WARN("os_file_delete(%.*s) - %s", arg8(path), strerror(errno));
	}

	arena_scratch_end(scratch);
	return result != 0;
}

uint64_t os_file_read_stream(OS_File file, void *buffer, uint64_t size) {
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

uint64_t os_file_write_stream(OS_File file, const void *buffer, uint64_t size) {
	uint8_t *running_offset = (uint8_t *)buffer;
	int64_t written_bytes = 0;

	while ((written_bytes = write(file, running_offset, size)) > 0) {
		size -= written_bytes;
		running_offset += written_bytes;
	}

	if (written_bytes == -1) {
		LOG_WARN("os_file_write - %s", strerror(errno));
	}

	return running_offset - (uint8_t *)buffer;
}

bool os_file_copy(string8 src, string8 dst) {
	OS_File input = 0, output = 0;

	bool ok = true;
	if (ok) {
		input = os_file_open(src, OS_FILE_MODE_READ);
		ok = os_file_valid(input);
	}

	if (ok) {
		output = os_file_open(dst, OS_FILE_MODE_READWRITE);
		ok = os_file_valid(output);
	}

	if (ok) {
		char buffer[4096] = { 0 };
		int64_t read_write_bytes = 0;
		while ((read_write_bytes = read(input, buffer, sizeof(buffer))) > 0)
			write(output, buffer, read_write_bytes);
	}

	os_file_close(input);
	os_file_close(output);

	return true;
}

string8 os_file_read(Arena *arena, string8 path) {
	string8 result = { 0 };

	OS_File handle = os_file_open(path, OS_FILE_MODE_READ);
	if (os_file_valid(handle)) {
		uint64_t size = os_file_size(handle);
		uint8_t *buffer = arena_push(arena, size + 1, 8, true);
		os_file_read_stream(handle, buffer, size);
		buffer[size] = '\0';

		result.bytes = buffer;
		result.length = size;

		os_file_close(handle);
	}

	return result;
}

void os_file_write(string8 path, const void *buffer, uint64_t size) {
	OS_File handle = os_file_open(path, OS_FILE_MODE_WRITE);
	if (os_file_valid(handle)) {
		os_file_write_stream(handle, buffer, size);
		os_file_close(handle);
	}
}

OS_Timestamp os_file_mtime(string8 path) {
	struct stat attrib;
	if (stat((char *)path.bytes, &attrib) == 0)
		return (uint64_t)attrib.st_mtime;

	return 0;
}

string8 os_current_directory(Arena *arena) {
	return os__concat_cwd(arena, s(""));
}

bool os_directory_exists(string8 path) {
	bool result = false;

	DIR *dir = os__open_dir(path);

	// ENOENT - Directory does not exist, or name is an empty string.
	if (dir) {
		result = true;
		closedir(dir);
	} else if (errno != ENOENT) {
		LOG_WARN("os_directory_exists - %s", strerror(errno));
	}

	return result;
}

bool os_directory_make(string8 path) {
	bool result = false;
	if (os_directory_exists(path) == false) {
		ArenaTemp scratch = arena_scratch_begin(NULL);
		bool relative = path.bytes[0] != '/';
		string8 full_path = relative ? os__concat_cwd(scratch.arena, path) : path;

		int32_t result = mkdir((char *)full_path.bytes, 0755);
		if (result == -1) {
			LOG_WARN("os_directory_make(%.*s) - %s", arg8(path), strerror(errno));
		}
		arena_scratch_end(scratch);

		result = true;
	}

	return result;
}

bool os_directory_delete(string8 path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	bool ok = os_directory_exists(path);
	if (ok) {
		int32_t result = rmdir((char *)os__concat_cwd(scratch.arena, path).bytes);

		ok = result != -1;
		if (ok == false)
			LOG_WARN("os_directory_make - %s", strerror(errno));
	}

	arena_scratch_end(scratch);
	return ok;
}

string8 *os_directory_files(Arena *arena, string8 path, uint32_t *count) {
	string8 *result = 0;

	bool ok = arena && count;
	if (ok) {
		*count = 0;

		ok = os_directory_exists(path);
	}

	DIR *dir = 0;
	if (ok) {
		dir = os__open_dir(path);

		ok = dir != 0;
	}

	if (ok) {
		struct dirent *entry = 0;

		while ((entry = readdir(dir)))
			if (entry->d_type == DT_REG) // This is a regular file
				(*count)++;
		rewinddir(dir);

		result = arena_push_count(arena, string8, *count);
		uint32_t cursor = 0;
		while ((entry = readdir(dir))) {
			if (entry->d_type == DT_REG) { // This is a regular file
				string8 path = {
					.length = strnlen(entry->d_name, 256),
				};

				path.bytes = arena_push_copy(arena, entry->d_name, path.length + 1, 1);
				result[cursor++] = path;
			}
		}
	}

	return result;
}

string8 os__concat_cwd(Arena *arena, string8 path) {
	ArenaTemp scratch = arena_scratch_begin(arena);
	string8 result = { 0 };

	uint32_t initial_size = 256;
	uint8_t *buffer = arena_push(scratch.arena, initial_size, 8, true);
	while (getcwd((void *)buffer, initial_size) == NULL) {
		initial_size += 256;
		arena_push(scratch.arena, 256, 1, true);
	}

	result = pathjoin8(arena, str8z((char *)buffer), path);
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

uint64_t os__open_file(string8 path, int32_t flags, int32_t access) {
	uint64_t result = OS_INVALID_FILE;
	int32_t open_result = open((char *)path.bytes, flags, access);

	if (open_result != -1)
		result = open_result;

	return result;
}

uint64_t os__open_fulllpath(string8 path, int32_t flags, int32_t access) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool relative = path.bytes[0] != '/';
	string8 fullpath = relative ? os__concat_cwd(scratch.arena, path) : path;

	uint64_t result = os__open_file(fullpath, flags, access);
	arena_scratch_end(scratch);

	return result;
}

DIR *os__open_dir(string8 path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	bool relative = path.bytes[0] != '/';
	string8 full_path = relative ? os__concat_cwd(scratch.arena, path) : path;

	DIR *result = opendir((char *)full_path.bytes);
	arena_scratch_end(scratch);

	return result;
}
