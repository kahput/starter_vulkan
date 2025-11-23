#pragma once

#include "event.h"

typedef struct {
	EventCommon header;

	uint32_t key, mods;
	bool is_repeat;
} KeyEvent;

typedef struct {
	EventCommon header;
	int32_t x, y;
	int32_t dx, dy;
} MouseMotionEvent;

typedef struct {
	EventCommon header;
	uint32_t button;
	int32_t x, y;
	uint32_t mods;
} MouseButtonEvent;

typedef struct {
	EventCommon header;
	uint32_t width, height;
} WindowResizeEvent;

EVENT_DEFINE(KeyEvent);
EVENT_DEFINE(MouseMotionEvent);
EVENT_DEFINE(MouseButtonEvent);
EVENT_DEFINE(WindowResizeEvent);
