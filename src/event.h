#pragma once

#include "common.h"

#define MAX_EVENT_SIZE 128

typedef struct {
	uint32_t type, size;
} EventCommon;

typedef struct {
	EventCommon header;

	uint8_t padding[MAX_EVENT_SIZE - sizeof(EventCommon)];
} Event;
#define EVENT_DEFINE(name) STATIC_ASSERT(sizeof(name) <= sizeof(Event), __LINE__)

typedef bool (*PFN_on_event)(Event *event);

bool event_system_startup(void);
bool event_system_shutdown(void);

void event_register(uint16_t event_type, PFN_on_event on_event);
void event_unregister(uint16_t event_type, PFN_on_event on_event);

#define event_create(T, type_id) ((T){ .header = { .type = type_id, .size = sizeof(T) } })
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
