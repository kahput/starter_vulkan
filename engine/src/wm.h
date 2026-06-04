#include "common.h"
#include "core/cmath.h"
#include "core/arena.h"
#include "core/strings.h"
#include "core/input_types.h"

struct Arena;

void os_startup(void);
void os_shutdown(void);

// ----------------------
// - System Information & Time
typedef struct {
	size_t page_size, large_page_size;
	uint32_t cpu_core_count;
} SystemInfo;

SystemInfo os_system_info(void);

uint64_t os_time_now(void); // tick
uint64_t os_time_frequency(void);
void os_sleep_ms(uint32_t ms); // ms

// ----------------------
// - Memory

void *os_memory_reserve(size_t size);
void os_memory_commit(void *ptr, size_t size);
void os_memory_decommit(void *ptr, size_t size);
void os_memory_release(void *ptr, size_t size);

// ----------------------
// - Files

typedef uint64_t OS_File;
#define OS_FILE_INVALID ((OS_File)0)
static inline bool os_file_valid(OS_File handle) { return handle != OS_FILE_INVALID; }

typedef enum {
	OS_FILE_READ = 0x1,
	OS_FILE_WRITE = 0x2,
	OS_FILE_READWRITE = 0x3,
} OS_FileMode;

OS_File os_file_open(String path, OS_FileMode);
void os_file_close(OS_File file);

uint64_t os_file_read(OS_File file, void *buffer, uint64_t size);
uint64_t os_file_write(OS_File file, const void *memory, uint64_t size);
uint64_t os_file_size(OS_File file);

String os_file_read_text(Arena *arena, String path);
Bytes os_file_read_binary(Arena *arena, String path);
void os_file_write_entire(String filename, const void *memory, uint64_t size);

bool os_file_exists(String path);
bool os_file_delete(String path);
bool os_file_copy(String src, String dst);
uint64_t os_file_last_modified(String filepath);

// ----------------------
// - Directories

bool os_directory_exists(String path);
bool os_directory_make(String path);
bool os_directory_delete(String path);

typedef enum {
	OS_ENTRY_TYPE_FILE,
	OS_ENTRY_TYPE_DIRECTORY,
	OS_ENTRY_TYPE_SYMLINK,
} OS_EntryType;

typedef struct {
	OS_EntryType type;
	String filename;
	uint64_t last_modified;
	uint64_t size;
} OS_DirectoryEntry;
OS_DirectoryEntry *os_directory_walk(Arena *arena, String path, uint32_t *count);

// ----------------------
// - Dynamic libraries
typedef uint64_t OS_Library;
#define OS_LIBRARY_INVALID ((OS_Library){ 0 })

OS_Library os_library_load(String path);
void os_library_unload(OS_Library lib);
void *os_library_symbol(OS_Library lib, const char *name);

// ----------------------
// - Draw surfaces/windows

typedef struct OS_Surface OS_Surface;
typedef enum {
	OS_EVENT_TYPE_NONE,
	OS_EVENT_TYPE_SURFACE_CLOSE,
	OS_EVENT_TYPE_SURFACE_RESIZE,
	OS_EVENT_TYPE_SURFACE_FOCUS_GAINED,
	OS_EVENT_TYPE_SURFACE_FOCUS_LOST,

	OS_EVENT_TYPE_KEY_PRESS,
	OS_EVENT_TYPE_KEY_RELEASE,

	OS_EVENT_TYPE_MOUSE_MOVE,
	OS_EVENT_TYPE_MOUSE_PRESS,
	OS_EVENT_TYPE_MOUSE_RELEASE,
	OS_EVENT_TYPE_MOUSE_SCROLL,
} OS_EventType;

typedef struct {
	OS_EventType type;
	OS_Surface *surface;
	union {
		struct {
			uint32_t width;
			uint32_t height;
		} resize;

		struct {
			KeyboardKey key_code;
			bool is_repeat;
		} key;

		struct {
			int32_t x;
			int32_t y;
			int32_t delta_x;
			int32_t delta_y;
		} mouse_move;
		struct {
			MouseButton button;
			int32_t x;
			int32_t y;
		} mouse_button;
		struct {
			float delta_x;
			float delta_y;
		} mouse_scroll;
	} as;
} OS_Event;

bool os_event_poll(OS_Event *out_event);

OS_Surface *os_surface_open(uint32_t width, uint32_t height, const char *title);
void os_surface_close(OS_Surface *surface);
bool os_surface_valid(OS_Surface *surface);

void os_surface_set_min(OS_Surface *surface, uint32_t width, uint32_t height);
void os_surface_set_max(OS_Surface *surface, uint32_t width, uint32_t height);
Rectangle os_client_rect(OS_Surface *surface);

/* void os_cursor_show(bool show); */
/* void os_cursor_capture(OS_Surface *surface, bool capture); */
/* void os_cursor_set_position(OS_Surface *surface, int32_t x, int32_t y); */

void *os_native_surface_handle(OS_Surface *surface);
