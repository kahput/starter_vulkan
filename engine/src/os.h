#pragma once

#include "common.h"
#include "core/arena.h"
#include "core/cmath.h"
#include "core/strings.h"
#include "core/input_types.h"

struct Arena;

typedef enum {
	OS_BACKEND_WINDOWS,
	OS_BACKEND_LINUX,
	OS_BACKEND_WEB,

	OS_BACKEND_COUNT,

#ifdef LINUX_BUILD
	OS_BACKEND = OS_BACKEND_LINUX,
#elif WINDOWS_BUILD
	OS_BACKEND = OS_BACKEND_WINDOWS,
#elif WEB_BUILD
	OS_BACKEND = OS_BACKEND_WEB,
#else
	#error Unsupported backend
#endif
} OS_Backend;

static String8 os_to_string[OS_BACKEND_COUNT] = {
	[OS_BACKEND_WINDOWS] = str_comp("windows"),
	[OS_BACKEND_LINUX] = str_comp("linux"),
	[OS_BACKEND_WEB] = str_comp("web"),
};

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
#define OS_INVALID_FILE ((OS_File)(0))
static inline bool os_file_valid(OS_File handle) { return handle != OS_INVALID_FILE; }

typedef enum {
	OS_FILE_MODE_READ,
	OS_FILE_MODE_WRITE,
	OS_FILE_MODE_READWRITE,
} OS_FileMode;

OS_File os_file_open(String8 path, OS_FileMode);
OS_File os_file_open_async(String8 path, OS_FileMode mode);

uint64_t os_file_size(OS_File handle);
void os_file_close(OS_File handle);

uint64_t os_file_read(OS_File file, void *buffer, uint64_t size);
uint64_t os_file_write(OS_File file, const void *buffer, uint64_t size);
bool os_file_copy(String8 src, String8 dst);

String8 os_file_read_entire(Arena *arena, String8 path);
void os_file_write_entire(String8 filename, const void *buffer, uint64_t size);

bool os_file_exists(String8 path);
bool os_file_delete(String8 path);

typedef uint64_t OS_Timestamp;
OS_Timestamp os_file_last_modified(String8 filepath);

String8 os_current_directory(Arena *arena);
bool os_directory_exists(String8 path);
bool os_directory_make(String8 path);
bool os_directory_delete(String8 path);

/* typedef enum { */
/* 	OS_ENTRY_TYPE_FILE, */
/* 	OS_ENTRY_TYPE_DIRECTORY, */
/* 	OS_ENTRY_TYPE_SYMLINK, */
/* } OS_EntryType; */

/* typedef struct { */
/* 	OS_EntryType type; */
/* 	String8 name; */
/* 	uint64_t last_modified; */
/* 	uint64_t size; */
/* } OS_DirectoryEntry; */

/* OS_DirectoryEntry *os_directory_walk(Arena *arena, String8 path, bool recurse, uint32_t *count); */
String8 *os_directory_files(Arena *arena, String8 path, uint32_t *count);

// ----------------------
// - Dynamic libraries
typedef void *OS_Library;
#define OS_LIBRARY_INVALID ((OS_Library)0)
static inline bool os_library_valid(OS_Library lib) { return lib != OS_LIBRARY_INVALID; }

OS_Library os_library_load(String8 path);
void os_library_unload(OS_Library lib);
bool os_library_symbol(OS_Library lib, String8 symbol, void *out_symbol);

// ----------------------
// - Draw surfaces/windows

typedef struct OS_Surface OS_Surface;
bool os_surface_valid(OS_Surface *surface);

bool os_display_startup(void);
void os_display_shutdown(void);

typedef enum {
	OS_SURFACE_FLAG_RESIZEABLE = 0x1,
} OS_SurfaceFlags;

OS_Surface *os_surface_open(uint32_t width, uint32_t height, String8 title, OS_SurfaceFlags flags);
OS_Surface *os_surface_open_with_parent(OS_Surface *parent, uint32_t width, uint32_t height, String8 title, OS_SurfaceFlags flags);
void os_surface_close(OS_Surface *surface);

void os_surface_show(OS_Surface *surface);
void os_surface_hide(OS_Surface *surface);

bool os_surface_minimized(OS_Surface *surface);
bool os_surface_drawable(OS_Surface *surface);

void os_surface_set_min(OS_Surface *surface, uint32_t width, uint32_t height);
void os_surface_set_max(OS_Surface *surface, uint32_t width, uint32_t height);

float os_surface_dpi(OS_Surface *surface);
uint2 os_surface_size(OS_Surface *surface);

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

const char **os_surface_vulkan_extensions(uint32_t *count);
void *os_native_display_handle(void);
void *os_native_surface_handle(OS_Surface *surface);

void os_cursor_show(OS_Surface *surface, bool show);
void os_cursor_capture(OS_Surface *surface, bool capture);
bool os_cursor_captured(OS_Surface *surface);
void os_cursor_set_position(OS_Surface *surface, int32_t x, int32_t y);
