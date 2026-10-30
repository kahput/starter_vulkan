#include "core/arena.h"
#include "core/debug.h"
#include "core/strings.h"
#include "core/os.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/limits.h>

#include <dirent.h>

int32_t os__file_open_flags_to_oflags(OS_FileOpenFlags mode);
OS_File os__open(string8 path, int32_t flags, int32_t access);
DIR *os__opendir(string8 path);

#include <signal.h>
void os_abort(void) {
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
}

string8 os_cwd(Arena *arena) {
	string8 result = { 0 };
	ArenaTemp scratch = arena_scratch_begin(arena);

	uint8_t buffer[PATH_MAX];
	if (getcwd((void *)buffer, PATH_MAX) == 0)
		LOG_ERROR("%s - %s", __func__, strerror(errno));
	else {
		string8 cwd = str8z((char *)buffer);
		result = str8_copy(arena, cwd);
	}

	arena_scratch_end(scratch);
	return result;
}

OS_File os_open(string8 filepath, OS_FileOpenFlags flags) {
	OS_File result = os__open(filepath, os__file_open_flags_to_oflags(flags), 0666);
	if (os_file_valid(result) == false) LOG_WARN("%s(\"%.*s\") - %s", __func__, arg8(filepath), strerror(errno));
	return result;
}

bool os_close(OS_File fd) {
	bool ok = os_file_valid(fd) && close(fd) >= 0;
	if (os_file_valid(fd) && ok == false)
		LOG_WARN("%s(%ld) - %s", __func__, fd, strerror(errno));

	return ok;
}

OS_Directory os_opendir(string8 path) {
	OS_Directory result = os__opendir(path);
	if (os_directory_valid(result) == false) LOG_WARN("%s(\"%.*s\") - %s", __func__, arg8(path), strerror(errno));
	return result;
}

bool os_closedir(OS_Directory dp) {
	bool ok = os_directory_valid(dp) && closedir(dp) >= 0;
	if (os_directory_valid(dp) && ok == false)
		LOG_WARN("%s(%p) - %s", __func__, dp, strerror(errno));

	return ok;
}

void os_rewinddir(OS_Directory dp) {
	if (os_directory_valid(dp)) rewinddir(dp);
}

uint64_t os_read(OS_File file, void *buffer, uint64_t size) {
	uint64_t result = 0;

	bool ok = buffer != 0 && size > 0;
	if (ok) {
		uint8_t *running_offset = buffer;

		int64_t read_bytes = 0;
		while ((read_bytes = read(file, running_offset, size)) > 0) {
			running_offset += read_bytes;
			size -= read_bytes;
		}
		if (read_bytes == -1) LOG_WARN("%s - %s", __func__, strerror(errno));

		result = read_bytes > 0 ? read_bytes : 0;
	}

	return result;
}

uint64_t os_write(OS_File file, const void *buffer, uint64_t size) {
	uint64_t result = 0;

	bool ok = buffer != 0 && size > 0;
	if (ok) {
		uint8_t *running_offset = (uint8_t *)buffer;
		int64_t written_bytes = 0;

		while ((written_bytes = write(file, running_offset, size)) > 0) {
			size -= written_bytes;
			running_offset += written_bytes;
		}
		if (written_bytes == -1) LOG_WARN("%s - %s", __func__, strerror(errno));

		result = written_bytes > 0 ? written_bytes : 0;
	}

	return result;
}

/* OS_File os_file_open_async(string8 path, OS_FileMode mode) { */
/* 	OS_File result = os__open_fulllpath(path, os__mode_to_flags(mode), 0666); */
/* 	if (os_file_valid(result) == false) { */
/* 		LOG_WARN("failed to read '%.*s' - %s", arg8(path), strerror(errno)); */
/* 	} */

/* 	return result; */
/* } */

uint64_t os_file_size(OS_File fd) {
	uint64_t result = 0;
	if (os_file_valid(fd)) {
		uint64_t original_offset = lseek(fd, 0, SEEK_CUR);
		result = lseek(fd, 0, SEEK_END);
		lseek(fd, original_offset, SEEK_SET);
	}

	return result;
}

bool os_readdir(OS_Directory dp, OS_DirectoryEntry *entry) {
	struct dirent *dent = 0;

	bool ok = os_directory_valid(dp) && entry;
	if (ok) {
		dent = readdir(dp);
		ok = dent != 0;
	}
	if (ok) {
		entry->name = str8z(dent->d_name);
		switch (dent->d_type) {
			case DT_DIR:
				entry->type = OS_ENTRY_DIR;
				break;
			case DT_LNK:
				entry->type = OS_ENTRY_LINK;
				break;
			case DT_REG:
				entry->type = OS_ENTRY_FILE;
				break;
			default:
				entry->type = OS_ENTRY_UNKNOWN;
				break;
		}
	}

	return ok;
}

bool os_file_exists(string8 filepath) {
	OS_File fd = os__open(filepath, O_RDONLY, 0);

	// ENOENT - O_CREAT is not set and the named file does not exist.
	bool result = os_file_valid(fd);
	if (result) os_close(fd);

	if (result == false && errno != ENOENT) {
		LOG_WARN("%s - %s", __func__, strerror(errno));
	}

	return result;
}

bool os_unlink(string8 path) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool relative = path.bytes[0] != '/';
	string8 fullpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), path) : str8_copy(scratch.arena, path);

	int32_t result = unlink((char *)fullpath.bytes);
	if (result < 0) LOG_WARN("%s(\"%.*s\") - %s", __func__, arg8(path), strerror(errno));

	arena_scratch_end(scratch);
	return result == 0;
}

bool os_rename(string8 from, string8 to) {
	ArenaTemp scratch = arena_scratch_begin(0);

	bool relative = from.bytes[0] != '/';
	string8 oldpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), from) : str8_copy(scratch.arena, from);

	relative = to.bytes[0] != '/';
	string8 newpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), to) : str8_copy(scratch.arena, to);

	int result = rename((char *)oldpath.bytes, (char *)newpath.bytes);
	if (result < 0) LOG_WARN("%s(from = \"%.*s\", to = \"%.*s\") - %s", __func__, arg8(from), arg8(to), strerror(errno));

	arena_scratch_end(scratch);
	return result == 0;
}

OS_Timestamp os_file_mtime(string8 path) {
	struct stat attrib;
	if (stat((char *)path.bytes, &attrib) == 0)
		return (uint64_t)attrib.st_mtime;

	return 0;
}

bool os_directory_exists(string8 path) {
	bool result = false;

	DIR *dir = os__opendir(path);

	// ENOENT - Directory does not exist, or name is an empty string.
	if (dir) {
		result = true;
		closedir(dir);
	} else if (errno != ENOENT) {
		LOG_WARN("os_directory_exists - %s", strerror(errno));
	}

	return result;
}

bool os_mkdir(string8 path) {
	bool result = false;
	if (os_directory_exists(path) == false) {
		ArenaTemp scratch = arena_scratch_begin(NULL);
		bool relative = path.bytes[0] != '/';
		string8 fullpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), path) : str8_copy(scratch.arena, path);

		int32_t result = mkdir((char *)fullpath.bytes, 0755);
		if (result == -1) {
			LOG_WARN("os_directory_make(%.*s) - %s", arg8(fullpath), strerror(errno));
		}
		arena_scratch_end(scratch);

		result = true;
	}

	return result;
}

bool os_rmdir(string8 path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	bool ok = os_directory_exists(path);
	if (ok) {
		bool relative = path.bytes[0] != '/';
		string8 fullpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), path) : str8_copy(scratch.arena, path);

		int32_t result = rmdir((char *)fullpath.bytes);

		ok = result != -1;
		if (ok == false)
			LOG_WARN("os_directory_make - %s", strerror(errno));
	}

	arena_scratch_end(scratch);
	return ok;
}

string8 *os_listdir(Arena *arena, string8 path, uint32_t *count) {
	string8 *result = 0;

	bool ok = arena && count;
	if (ok) {
		*count = 0;

		ok = os_directory_exists(path);
	}

	DIR *dir = 0;
	if (ok) {
		dir = os__opendir(path);
		ok = dir != 0;
	}

	if (ok) {
		struct dirent *entry = 0;

		while ((entry = readdir(dir)))
			if (entry->d_type == DT_REG || entry->d_type == DT_LNK) // This is a regular file
				(*count)++;
		rewinddir(dir);

		result = arena_push_count(arena, string8, *count);
		uint32_t cursor = 0;
		while ((entry = readdir(dir))) {
			if (entry->d_type == DT_REG || entry->d_type == DT_LNK) { // This is a regular file
				string8 path = {
					.length = strnlen(entry->d_name, 256),
				};

				path.bytes = arena_push_copy(arena, entry->d_name, path.length + 1);
				result[cursor++] = path;
			}
		}
	}

	return result;
}

int32_t os__file_open_flags_to_oflags(OS_FileOpenFlags flags) {
	int32_t result = 0;

	if (flags & OS_RDONLY) result |= O_RDONLY;
	if (flags & OS_WRONLY) result |= O_WRONLY;
	if (flags & OS_CREAT) result |= O_CREAT;
	if (flags & OS_TRUNC) result |= O_TRUNC;
	if (flags & OS_APPEND) result |= O_APPEND;
	if (flags & OS_NONBLOCK) result |= O_NONBLOCK;

	return result;
}

OS_File os__open(string8 path, int32_t flags, int32_t access) {
	ArenaTemp scratch = arena_scratch_begin(NULL);
	bool relative = path.bytes[0] != '/';
	string8 fullpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), path) : str8_copy(scratch.arena, path);

	OS_File result = open((char *)path.bytes, flags, access);
	arena_scratch_end(scratch);

	return result;
}

DIR *os__opendir(string8 path) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	bool relative = path.bytes[0] != '/';
	string8 fullpath = relative ? str8_pathjoin(scratch.arena, os_cwd(scratch.arena), path) : str8_copy(scratch.arena, path);

	DIR *result = opendir((char *)fullpath.bytes);
	arena_scratch_end(scratch);

	return result;
}
