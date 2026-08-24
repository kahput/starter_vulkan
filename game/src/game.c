#include "core/debug.h"
#include "core/input_types.h"
#include "core/strings.h"
#include "gfx/gfx_types.h"
#include "tables.h"
#include "types.h"

#include <common.h>
#include <core/arena.h>
#include <core/logger.h>

#include <os.h>

#include <gfx.h>

#include <draw.h>
#include <draw/font.h>
#include <draw/imgui.h>

#include <unistd.h>
#include <utils/input.h>
#include <utils/anim.h>

#include "helper.c"

typedef struct {
	Arena *permanent, *frame;
	GFX_Device device[1];

	OS_Surface *surface;
	GFX_Swapchain *swapchain;

	InputState input;

	uint64_t start_time;
	float dt, last_frame;

	bool initialized;
} State;

State *state = 0;
static inline char *named(const char *name) {
	return (char *)str8_pushf(state->permanent, str8_wrap(name)).text;
}

bool tick(Arena *permanent, Arena *frame) {
	state = (State *)permanent->base;
	input_set_context(&state->input);

	if (state->initialized == false) {
		arena_push_count(permanent, State, 1); // reserve space for state
		state->permanent = permanent, state->frame = frame;

		gfx_device_make(state->device);

		state->surface = os_surface_open(1280, 720, str8_wrap(named("game")), OS_SURFACE_FLAG_RESIZEABLE);
		state->swapchain = gfx_swapchain_make(state->device, state->surface, named("main"));

		state->initialized = true;
		state->start_time = os_time_ns();
	}
	float time = (os_time_ns() * 1e-9) - (state->start_time * 1e-9);
	state->dt = time - state->last_frame;
	state->last_frame = time;
	input_update();

	for (OS_Event event; os_event_poll(&event);) {
		switch (event.type) {
			case OS_EVENT_TYPE_SURFACE_CLOSE:
				return false;
				break;
			case OS_EVENT_TYPE_SURFACE_RESIZE:
				break;

			case OS_EVENT_TYPE_KEY_PRESS:
			case OS_EVENT_TYPE_KEY_RELEASE:
				input_feed_key(event.as.key.key_code, event.type == OS_EVENT_TYPE_KEY_PRESS);
				break;

			case OS_EVENT_TYPE_MOUSE_MOVE:
				input_feed_mouse_motion(event.as.mouse_move.x, event.as.mouse_move.y);
				break;

			case OS_EVENT_TYPE_MOUSE_PRESS:
			case OS_EVENT_TYPE_MOUSE_RELEASE:
				if (event.type == OS_EVENT_TYPE_MOUSE_RELEASE) {
					uint32_t x = 0;
					(void)x;
				}
				input_feed_mouse_button(event.as.mouse_button.button, event.type == OS_EVENT_TYPE_MOUSE_PRESS);
				break;

			default:
				break;
		}
	}

	return true;
}
