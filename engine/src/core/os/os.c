#include "core/os.h"

string8 os_file_read(Arena *arena, string8 path) {
	string8 result = { 0 };

	OS_File fd = os_open(path, OS_RDONLY);
	if (os_file_valid(fd)) {
		uint64_t size = os_file_size(fd);
		uint8_t *buffer = arena_push(arena, size + 1, 8, true);
		os_read(fd, buffer, size);
		buffer[size] = '\0';

		result.bytes = buffer;
		result.length = size;

		os_close(fd);
	}

	return result;
}

void os_file_write(string8 path, string8 buf) {
	OS_File fd = os_open(path, OS_WRONLY | OS_CREAT | OS_TRUNC);
	if (os_file_valid(fd)) {
		os_write(fd, buf.bytes, buf.length);
		os_close(fd);
	}
}
void os_file_append(string8 filename, string8 buf) {
	OS_File fd = os_open(filename, OS_APPEND | OS_WRONLY);
	if (os_file_valid(fd)) {
		os_write(fd, buf.bytes, buf.length);
		os_close(fd);
	}
}

bool os_file_copy(string8 src, string8 dst) {
	OS_File input = os_open(src, OS_RDONLY), output = os_open(dst, OS_RDWR | OS_CREAT | OS_TRUNC);

	ArenaTemp scratch = arena_scratch_begin(0);
	bool ok = os_file_valid(input) && os_file_valid(output);
	if (ok) {
		uint64_t buffer_size = os_file_size(input);
		void *buffer = arena_push_count(scratch.arena, char, buffer_size);

		os_read(input, buffer, buffer_size);
		os_write(output, buffer, buffer_size);
	}
	arena_scratch_end(scratch);

	os_close(input);
	os_close(output);

	return true;
}

#ifdef LINUX_BUILD
/* #define _POSIX_C_SOURCE 200809L */
/* #define _DEFAULT_SOURCE */

	#include "linux/filesystem.c"
	#include "linux/dynlib.c"
	#include "linux/time.c"
	#include "linux/memory.c"
#elif WEB_BUILD
	#include "web/filesystem.c"
	#include "web/dynlib.c"
	#include "web/time.c"
	#include "web/memory.c"
#elif WINDOWS_BUILD
	#include "win32/filesystem.c"
	#include "win32/dynlib.c"
	#include "win32/time.c"
	#include "win32/memory.c"
#endif
