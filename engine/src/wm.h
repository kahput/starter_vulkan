#pragma once

#include "common.h"
#include "core/strings.h"
#include "core/input_types.h"

// ----------------------
// - Draw surfaces/windows

typedef struct WM_Surface WM_Surface;
bool wm_surface_valid(WM_Surface *surface);

bool wm_startup(void);
void wm_shutdown(void);

typedef enum {
	WM_SURFACE_FLAG_RESIZEABLE = 0x1,
} WM_SurfaceFlags;

WM_Surface *wm_surface_open(uint32_t width, uint32_t height, string8 title, WM_SurfaceFlags flags);
WM_Surface *wm_surface_open_with_parent(WM_Surface *parent, uint32_t width, uint32_t height, string8 title, WM_SurfaceFlags flags);
void wm_surface_close(WM_Surface *surface);

void wm_surface_show(WM_Surface *surface);
void wm_surface_hide(WM_Surface *surface);

bool wm_surface_minimized(WM_Surface *surface);
bool wm_surface_drawable(WM_Surface *surface);

float wm_surface_dpi(WM_Surface *surface);
uint2 wm_surface_size(WM_Surface *surface);

typedef enum {
	WM_EVENT_TYPE_NONE,
	WM_EVENT_TYPE_SURFACE_CLOSE,
	WM_EVENT_TYPE_SURFACE_RESIZE,
	WM_EVENT_TYPE_SURFACE_FOCUS_GAINED,
	WM_EVENT_TYPE_SURFACE_FOCUS_LOST,

	WM_EVENT_TYPE_KEY_PRESS,
	WM_EVENT_TYPE_KEY_RELEASE,

	WM_EVENT_TYPE_MOUSE_MOTION,
	WM_EVENT_TYPE_MOUSE_PRESS,
	WM_EVENT_TYPE_MOUSE_RELEASE,
	WM_EVENT_TYPE_MOUSE_SCROLL,
} WM_EventType;

// clang-format off
typedef struct {
	WM_EventType type;
	WM_Surface *surface;
	union {
		struct { uint32_t width; uint32_t height; } resize;
		struct { KeyCode key_code; bool is_repeat; } key;
		struct { int32_t x, y; float dx, dy; } mouse_motion;
		struct { MouseButton button; int32_t x; int32_t y; } mouse_button;
		struct { float dx; float dy; } mouse_scroll;
	} as;
} WM_Event;
bool wm_poll(WM_Event *out);
// clang-format on

const char **wm_vulkan_surface_extensions(uint32_t *count);
void *wm_native_display(void);
void *wm_native_surface(WM_Surface *surface);

void wm_cursor_show(WM_Surface *surface, bool show);
void wm_cursor_capture(WM_Surface *surface, bool capture);
bool wm_cursor_captured(WM_Surface *surface);
void wm_cursor_set_position(WM_Surface *surface, int32_t x, int32_t y);

typedef struct {
	bool connected;
	uint32_t buttons;
	int16_t axes[GAMEPAD_AXIS_MAX];
} GP_Slot;

void gp_startup(void);
void gp_shutdown(void);

#define GP_SLOT_MAX 8
void gp_poll(GP_Slot slots[GP_SLOT_MAX]);
