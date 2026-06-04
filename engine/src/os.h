#include "common.h"
#include "core/arena.h"
#include "core/strings.h"
#include "core/cmath.h"

struct Arena;

// ----------------------
// - System Information & Time
/* typedef struct { */
/* 	size_t page_size, large_page_size; */
/* 	uint32_t cpu_core_count; */
/* } SystemInfo; */

/* SystemInfo os_system_info(void); */

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

typedef uint64_t OS_File;
#define OS_FILE_INVALID ((OS_File)(-1))
static inline bool os_file_valid(OS_File handle) { return handle != OS_FILE_INVALID; }

typedef enum {
	OS_FILE_MODE_READ,
	OS_FILE_MODE_WRITE,
	OS_FILE_MODE_READWRITE,
} OS_FileMode;

OS_File os_file_open(String path, OS_FileMode);
uint64_t os_file_size(OS_File handle);
void os_file_close(OS_File handle);

uint64_t os_file_read(OS_File file, void *buffer, uint64_t size);
uint64_t os_file_write(OS_File file, const void *buffer, uint64_t size);
bool os_file_copy(String src, String dst);

String os_file_read_entire(Arena *arena, String path);
void os_file_write_entire(String filename, const void *buffer, uint64_t size);

bool os_file_exists(String path);
bool os_file_delete(String path);
uint64_t os_file_last_modified(String filepath);
String os_current_directory(Arena *arena);

bool os_directory_exists(String path);
bool os_directory_make(String path);
bool os_directory_delete(String path);

// OS_DirectoryList os_directory_walk(Arena *arena, String root, bool recurse);

// ----------------------
// - Dynamic libraries
typedef void *OS_Library;
#define OS_LIBRARY_INVALID ((OS_Library)0)
static inline bool os_library_valid(OS_Library lib) { return lib != OS_LIBRARY_INVALID; }

OS_Library os_library_load(String path);
void os_library_unload(OS_Library lib);
void os_library_symbol(OS_Library lib, String symbol, void *out_symbol);
