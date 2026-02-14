#pragma once

#include "event.h"

typedef struct {
	uint32_t key, mods;
	bool leave;
} KeyEvent;

typedef struct {
    double x, y;
	double dx, dy;

	bool virtual_cursor;
} MouseMotionEvent;

typedef struct {
	uint32_t button;
	double x, y;
	uint32_t mods;
} MouseButtonEvent;

typedef struct {
	uint32_t width, height;
} WindowResizeEvent;

enum {
	EVENT_PLATFORM_WINDOW_RESIZED = EVENT_ID(EVENT_SUBSYSTEM_PLATFORM, 0x01),

	EVENT_PLATFORM_KEY_PRESSED,
	EVENT_PLATFORM_KEY_RELEASED,

	EVENT_PLATFORM_MOUSE_MOTION,
	EVENT_PLATFORM_MOUSE_BUTTON_PRESSED,
	EVENT_PLATFORM_MOUSE_BUTTON_RELEASED,
};

EVENT_STRUCT_DECLARE(key_event, KeyEvent)
EVENT_STRUCT_DECLARE(mouse_motion, MouseMotionEvent)
EVENT_STRUCT_DECLARE(mouse_button, MouseButtonEvent)
EVENT_STRUCT_DECLARE(mouse_resize, WindowResizeEvent)
