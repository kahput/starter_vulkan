#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/strings.h"

typedef enum {
	OS_BACKEND_WINDOWS,
	OS_BACKEND_LINUX,
	OS_BACKEND_WEB,

	OS_BACKEND_MAX,

#ifdef LINUX_BUILD
	OS_BACKEND = OS_BACKEND_LINUX,
#elif WINDOWS_BUILD
	OS_BACKEND = OS_BACKEND_WINDOWS,
#elif WEB_BUILD
	OS_BACKEND = OS_BACKEND_WEB,
#endif
} OS_Backend;

static string8 os_backend_to_display_string[OS_BACKEND_MAX] = {
	[OS_BACKEND_WINDOWS] = comp8("Windows"),
	[OS_BACKEND_LINUX] = comp8("Linux"),
	[OS_BACKEND_WEB] = comp8("Web"),
};

// ----------------------
// - System Information & Time
/* typedef struct { */
/* 	size_t page_size, large_page_size; */
/* 	uint32_t cpu_core_count; */
/* } SystemInfo; */

/* SystemInfo os_system_info(void); */

void os_abort(void);

uint64_t os_time_ns(void); // nanoseconds
void os_sleep_ms(uint32_t ms);

// ----------------------
// - Memory

void *os_memory_reserve(size_t size);
void os_memory_commit(void *ptr, size_t size);
void os_memory_decommit(void *ptr, size_t size);
void os_memory_release(void *ptr, size_t size);

// ----------------------
// - File system

typedef int64_t OS_File;
#define OS_INVALID_FILE ((OS_File)(-1))
static inline bool os_file_valid(OS_File fd) { return fd != OS_INVALID_FILE; }

typedef void *OS_Directory;
#define OS_INVALID_DIR ((OS_Directory)(0))
static inline bool os_directory_valid(OS_Directory dp) { return dp != OS_INVALID_DIR; }

typedef enum {
	OS_ENTRY_UNKNOWN,

	OS_ENTRY_FILE,
	OS_ENTRY_DIR,
	OS_ENTRY_LINK,
} OS_EntryType;

typedef struct {
	OS_EntryType type;
	string8 name;
} OS_DirectoryEntry;

typedef enum {
	OS_RDONLY = BIT(0),
	OS_WRONLY = BIT(1),
	OS_CREAT = BIT(2),
	OS_TRUNC = BIT(3),
	OS_APPEND = BIT(4),
	OS_NONBLOCK = BIT(5),
	OS_BINARY = BIT(6),

	OS_RDWR = OS_RDONLY | OS_WRONLY,
} OS_FileOpenFlags;

OS_File os_open(string8 path, OS_FileOpenFlags flags); // TODO: Mode
bool os_close(OS_File fd);

OS_Directory os_opendir(string8 path);
bool os_closedir(OS_Directory dp);

uint64_t os_read(OS_File fd, void *buffer, uint64_t size);
uint64_t os_write(OS_File fd, const void *buffer, uint64_t size);
uint64_t os_file_size(OS_File fd);

bool os_readdir(OS_Directory dp, OS_DirectoryEntry *entry);
void os_rewinddir(OS_Directory dp);

// file
string8 os_file_read(Arena *arena, string8 path);
void os_file_write(string8 filename, string8 buf);
void os_file_append(string8 filename, string8 buf);
bool os_file_copy(string8 src, string8 dst);

bool os_unlink(string8 path);
bool os_rename(string8 path, string8 to);
bool os_file_exists(string8 path);

typedef uint64_t OS_Timestamp;
OS_Timestamp os_file_mtime(string8 filepath);

string8 os_cwd(Arena *arena);
bool os_mkdir(string8 path);
bool os_directory_exists(string8 path);
bool os_rmdir(string8 path);

/* OS_DirectoryEntry *os_directory_walk(Arena *arena, String8 path, bool recurse, uint32_t *count); */
string8 *os_listdir(Arena *arena, string8 path, uint32_t *count);

// ----------------------
// - Dynamic libraries
typedef void *OS_Library;
#define OS_LIBRARY_INVALID ((OS_Library)0)
static inline bool os_library_valid(OS_Library lib) { return lib != OS_LIBRARY_INVALID; }

OS_Library os_dlopen(string8 path);
void os_dlclose(OS_Library lib);
bool os_dlsym(OS_Library lib, string8 symbol, void *out_symbol);

int32_t os_execute_command(string8 cmd);
