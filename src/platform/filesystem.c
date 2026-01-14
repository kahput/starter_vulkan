#include "filesystem.h"
#include "core/arena.h"
#include "core/astring.h"
#include "core/logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "dirent.h"

bool filesystem_file_exists(String path) {
	FILE *file = fopen((const char *)path.data, "r");
	if (file) {
		fclose(file);
		return true;
	}
	return false;
}

FileContent filesystem_read(Arena *arena, String path) {
	FILE *file = fopen((const char *)path.data, "rb");
	if (file == NULL) {
		LOG_ERROR("Failed to read file '%s': %s", path.data, strerror(errno));
		return (FileContent){ 0 };
	}

	fseek(file, 0L, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0L, SEEK_SET);

	char *byte_content = arena_push_array_zero(arena, char, size);
	fread(byte_content, sizeof(*byte_content), size, file);

	fclose(file);

	return (FileContent){ .size = size, .content = byte_content };
}

FileNode *filesystem_load_directory_files(Arena *arena, String directory_path, bool recursive) {
	DIR *directory = opendir(directory_path.data);
	if (!directory)
		return NULL;

	FileNode *list_head = NULL;
	FileNode **list_tail = &list_head;

	ArenaTemp scratch = arena_scratch(arena);

	struct dirent *entry;

	while ((entry = readdir(directory))) {
		String entry_name = string_wrap_cstring(entry->d_name);
		if (string_equals(entry_name, S(".")) || string_equals(entry_name, S("..")))
			continue;

		String full_path = string_concat(scratch.arena, directory_path, S("/"));
		full_path = string_concat(scratch.arena, full_path, entry_name);

		struct stat info;
		if (stat(full_path.data, &info) != 0)
			continue;

		if (S_ISDIR(info.st_mode) == false) {
			FileNode *new_node = arena_push_struct(arena, FileNode);
			new_node->path = string_duplicate(arena, full_path);
			new_node->next = NULL;

			*list_tail = new_node;
			list_tail = &new_node->next;
		}

		if (recursive && S_ISDIR(info.st_mode)) {
			FileNode *sub_directory_files = filesystem_load_directory_files(arena, full_path, true);

			if (sub_directory_files) {
				*list_tail = sub_directory_files;

				while (*list_tail) {
					list_tail = &(*list_tail)->next;
				}
			}
		}
	}

	closedir(directory);
	arena_release_scratch(scratch);
	return list_head;
}

uint64_t filesystem_last_modified(String path) {
	struct stat attr;
	stat((const char *)path.data, &attr);

	return (uint64_t)(attr.st_mtim.tv_sec * 1000ULL + attr.st_mtim.tv_nsec / 1000000ULL);
}
