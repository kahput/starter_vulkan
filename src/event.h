#pragma once

#include "common.h"

typedef struct {
	uint64_t type;
	uint32_t size, timestamp;
} EventHeader;

typedef union {
	EventHeader header;
	int64_t payload[8];
} Event;

#define EVENT_DEFINE(name) STATIC_ASSERT(sizeof(name) <= sizeof(Event), __FILE__)

typedef bool (*PFN_on_event)(Event *event);

bool event_system_startup(void);
bool event_system_shutdown(void);

void event_register(uint16_t event_type, PFN_on_event on_event);
void event_unregister(uint16_t event_type, PFN_on_event on_event);

void event_emit(Event *event);

typedef enum {
	SV_EVENT_NULL = 0,

	SV_EVENT_QUIT,
	SV_EVENT_WINDOW_RESIZED,

	SV_EVENT_KEY_PRESSED,
	SV_EVENT_KEY_RELEASED,

	SV_EVENT_MOUSE_MOTION,
	SV_EVENT_MOUSE_BUTTON_PRESSED,
	SV_EVENT_MOUSE_BUTTON_RELEASED,

	MAX_SV_EVENT = 0xFF
} CoreEvent;
