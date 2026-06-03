#include "core/debug.h"
#include "os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dirent.h"

void os_concat_cwd(String path, char *dst, size_t dst_size) {
	getcwd(dst, dst_size);
}

bool os_file_exists(String filepath) {
	bool result = true;
	int32_t fd = open(filepath.text, O_RDONLY);
	// ENOENT - O_CREAT is not set and the named file does not exist.
	if (errno == ENOENT)
		result = false;

	fd = close(fd);
	return result;
}

FileHandle os_file_open(String filepath, OS_FileMode mode) {
	FileHandle result = { 0 };

	int32_t flag = 0;
	switch (mode) {
		case OS_FILE_MODE_READ:
			flag = O_RDONLY;
			break;
		case OS_FILE_MODE_WRITE:
			flag = O_WRONLY;
			break;
		case OS_FILE_MODE_READWRITE:
			flag = O_RDWR;
			break;
	}

	char buffer[512] = { 0 };
	result = open(filepath.text, flag);
	if (result == (uint64_t)-1) {
		LOG_WARN("%s", strerror(errno));
	}

	return result;
}
void os_file_close(FileHandle file) {
	// EBADF - fd isn't a valid open file descriptor.
	if (close(file) == -1) {
		LOG_WARN("%s", strerror(errno));
	}
}
uint64_t os_file_write(FileHandle file, uint8_t *memory, uint64_t size);

String os_file_read_text(Arena *arena, String filepath);
Bytes os_file_read_binary(Arena *arena, String filepath);
void os_file_write_entire(String filename, void *memory, uint64_t size);

bool os_file_delete(String filepath) {
	NOT_IMPLEMENTED;
}

bool os_file_copy(String src, String dst) {
	int input = open(src.text, O_RDONLY);
	if (input == -1)
		return false;

	int output = creat(dst.text, 0666);
	if (output == -1) {
		close(input);
		return false;
	}

	char buffer[4096];
	ssize_t bytes;
	while ((bytes = read(input, buffer, sizeof(buffer))) > 0) {
		write(output, buffer, bytes);
	}

	close(input);
	close(output);
	return true;
}

uint64_t os_file_last_modified(String filefilepath) {
	struct stat attrib;
	if (stat(filefilepath.text, &attrib) == 0)
		return (uint64_t)attrib.st_mtime;

	return 0;
}

bool os_directory_exists(String dir_path);
bool os_directory_create(String dir_path);
bool os_directory_delete(String dir_path);
