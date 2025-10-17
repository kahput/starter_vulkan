#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct event_context {
	// 128 bytes
	union {
		int64_t int64[2];
		uint64_t uint64[2];
		double float64[2];

		int32_t int32[4];
		uint32_t uint32[4];
		float float32[4];

		int16_t int16[8];
		uint16_t uint16[8];

		int8_t int8[16];
		uint8_t uint8[16];

		const char s[16];
	} data;
} event_context;

typedef bool (*PFN_on_event)(uint16_t code, void *sender, void *listener_inst, event_context data);

bool event_system_initialize(uint64_t *memory_requirement, void *state, void *config);

void event_system_shutdown(void *state);

bool event_register(uint16_t code, void *listener, PFN_on_event on_event);

bool event_unregister(uint16_t code, void *listener, PFN_on_event on_event);

bool event_fire(uint16_t code, void *sender, event_context context);

typedef enum {
	SYSTEM_EVENT_APPLICATION_QUIT = 0x01,

	SYSTEM_EVENT_KEY_PRESSED = 0x02,
	SYSTEM_EVENT_KEY_RELEASED = 0x03,

	SYSTEM_EVENT_BUTTON_PRESSED = 0x04,
	SYSTEM_EVENT_BUTTON_RELEASED = 0x05,
	SYSTEM_EVENT_BUTTON_CLICKED = 0x06,

	SYSTEM_EVENT_MOUSE_MOVED = 0x07,
	SYSTEM_EVENT_MOUSE_WHEEL = 0x08,

	SYSTEM_EVENT_WINDOW_RESIZED = 0x09,

	SYSTEM_EVENT_MAX = 0xFF
} SystemEvent;
