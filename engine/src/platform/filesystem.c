#include "filesystem.h"
#include "core/arena.h"
#include "core/strings.h"
#include "core/logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dirent.h"

bool filesystem_file_exists(String path) {
	FILE *file = fopen((const char *)path.memory, "r");
	if (file) {
		fclose(file);
		return true;
	}
	return false;
}

String filesystem_read(Arena *arena, String path) {
	FILE *file = fopen((const char *)path.memory, "rb");
	if (file == NULL) {
		LOG_ERROR("Failed to read file '%s': %s", path.memory, strerror(errno));
		return (String){ 0 };
	}

	fseek(file, 0L, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0L, SEEK_SET);

	char *byte_content = arena_push_count(arena, size + 1, char);
	fread(byte_content, sizeof(*byte_content), size, file);
	byte_content[size] = '\0';

	fclose(file);

	return (String){ .memory = byte_content, .length = size };
}

bool filesystem_file_copy(String from, String to) {
	int input = open(from.memory, O_RDONLY);
	if (input == -1)
		return false;

	// 0666 = Read/Write permissions
	int output = open(to.memory, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (output == -1) {
		close(input);
		return false;
	}

	// Simple buffer copy
	char buffer[4096];
	ssize_t bytes;
	while ((bytes = read(input, buffer, sizeof(buffer))) > 0) {
		write(output, buffer, bytes);
	}

	close(input);
	close(output);
	return true;
}

StringList filesystem_list_files(Arena *arena, String directory_path, bool recursive) {
	StringList list = { 0 };
	ArenaTemp scratch = arena_scratch_begin(arena);

	char *dir_cstr = string_cstring(scratch.arena, directory_path);
	DIR *directory = opendir(dir_cstr);

	if (!directory) {
		arena_scratch_end(scratch);
		return list;
	}

	struct dirent *entry;
	while ((entry = readdir(directory))) {
		String entry_name = string_wrap(entry->d_name);

		if (string_equals(entry_name, S(".")) ||
			string_equals(entry_name, S(".."))) {
			continue;
		}

		String temp_full_path = stringpath_join(scratch.arena, directory_path, entry_name); // All string_push* calls guarantee \0 terminated

		struct stat info;
		if (stat(temp_full_path.memory, &info) != 0) {
			continue;
		}

		if (S_ISDIR(info.st_mode)) {
			if (recursive) {
				StringList sub_list = filesystem_list_files(arena, temp_full_path, true);

				if (sub_list.count > 0) {
					if (list.last)
						list.last->next = sub_list.first;
					else
						list.first = sub_list.first;

					list.last = sub_list.last;
					list.count += sub_list.count;
					list.total_length += sub_list.total_length;
				}
			}
		} else {
			String permanent_path = stringpath_join(arena, directory_path, entry_name);
			stringlist_push(arena, &list, permanent_path);
		}
	}

	closedir(directory);
	arena_scratch_end(scratch);
	return list;
}

uint64_t filesystem_last_modified(String path) {
	struct stat attrib;
	if (stat(path.memory, &attrib) == 0) {
		return (uint64_t)attrib.st_mtime;
	}
	return 0;
}
