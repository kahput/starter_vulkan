#include "common.h"
#include "core/arena.h"
#include "core/strings.h"
#include "core/cmath.h"

struct Arena;

// ----------------------
// - System Information & Time
typedef struct {
	size_t page_size, large_page_size;
	uint32_t cpu_core_count;
} SystemInfo;

SystemInfo os_system_info(void);

uint64_t os_time_now(void); // tick
void os_sleep(uint32_t ms); // ms

// ----------------------
// - Memory

void *os_memory_reserve(size_t size);
void os_memory_commit(void *ptr, size_t size);
void os_memory_decommit(void *ptr, size_t size);
void os_memory_release(void *ptr, size_t size);

// ----------------------
// - File handling

typedef uint64_t FileHandle;
#define OS_INVALID_FILE_HANDLE ((FileHandle)0)

typedef enum {
	OS_FILE_MODE_READ,
	OS_FILE_MODE_WRITE,
	OS_FILE_MODE_READWRITE,
} OS_FileMode;

FileHandle os_file_open(String path, OS_FileMode);
void os_file_close(FileHandle file);

uint64_t os_file_read(FileHandle file, void *buffer, uint64_t size);
uint64_t os_file_write(FileHandle file, uint8_t *memory, uint64_t size);

String os_file_read_text(Arena *arena, String path);
Bytes os_file_read_binary(Arena *arena, String path);
void os_file_write_entire(String filename, void *memory, uint64_t size);

bool os_file_exists(String path);
bool os_file_delete(String path);
bool os_file_copy(String src, String dst);
uint64_t os_file_last_modified(String filepath);

bool os_directory_exists(String path);
bool os_directory_create(String path);
bool os_directory_delete(String path);
