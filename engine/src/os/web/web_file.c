#include "core/debug.h"
#include "core/logger.h"
#include "os.h"
#include <errno.h>
#include <stdio.h>

const char *os__mode_to_string(OS_FileMode mode) {
	return mode == OS_FILE_MODE_READWRITE ? "rw" : mode == OS_FILE_MODE_READ ? "r"
																			 : "w";
}

OS_File os_file_open(String8 path, OS_FileMode mode) {
	FILE *file = fopen(path.text, os__mode_to_string(mode));
	OS_File result = (OS_File)file;

	if (file == NULL) {
		result = OS_FILE_INVALID;
		LOG_WARN("%s - %s", __func__, strerror(errno));
	}
	return result;
}

uint64_t os_file_size(OS_File handle) {
	FILE *file = (FILE *)handle;
	uint64_t original_offset = ftell(file);
	fseek(file, 0, SEEK_END);
	uint64_t result = ftell(file);
	fseek(file, original_offset, SEEK_SET);

	return result;
}

void os_file_close(OS_File handle) {
	FILE *file = (FILE *)handle;
	if (file)
		fclose(file);
}

uint64_t os_file_read(OS_File handle, void *buffer, uint64_t size) {
	return fread(buffer, size, 1, (FILE *)handle);
}

uint64_t os_file_write(OS_File handle, const void *buffer, uint64_t size) {
	return fwrite(buffer, size, 1, (FILE *)handle);
}

bool os_file_copy(String8 src, String8 dst) {
	NOT_IMPLEMENTED;
}

String8 os_file_read_entire(Arena *arena, String8 path) {
	OS_File handle = os_file_open(path, OS_FILE_MODE_READ);
	uint64_t size = os_file_size(handle);

	void *buffer = arena_push(arena, size + 1, 1, false);
	os_file_read(handle, buffer, size);
	((uint8_t *)buffer)[size] = '\0';

	os_file_close(handle);
	return (String8){ .text = buffer, .length = size };
}

void os_file_write_entire(String8 path, const void *buffer, uint64_t size) {
	OS_File handle = os_file_open(path, OS_FILE_MODE_WRITE);
	uint64_t result = os_file_write(handle, buffer, size); // TODO: Return written bytes
	os_file_close(handle);
}

bool os_file_exists(String8 path) {
	bool result = false;
	OS_File handle = os_file_open(path, OS_FILE_MODE_READ);
	if (handle != OS_FILE_INVALID) {
		result = true;
		os_file_close(handle);
	}

	return result;
}

bool os_file_delete(String8 path);
uint64_t os_file_last_modified(String8 filepath);
String8 os_current_directory(Arena *arena);

bool os_directory_exists(String8 path);
bool os_directory_make(String8 path);
bool os_directory_delete(String8 path);
