#include "core/arena.h"
#include "input.h"

#include "os.h"
#include "gfx.h"
#include "gfx/gfx_types.h"

#include "core/debug.h"
#include "core/logger.h"

typedef bool (*TickFn)(Arena *arena, InputState *input);

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	String8 src = s("libgame.so"), dst = s("libgame_loaded.so");

	OS_Library lib = 0;
	TickFn tick = 0;
	OS_Timestamp ts = 0;

	os_display_startup();
	GFX_Device gfx = { 0 };
	gfx_device_make(&gfx);

	InputState input_state = { 0 };
	input_set_context(&input_state);

	Arena arena = arena_make(MiB(16));

	for (bool is_open = true; is_open;) {
		OS_Timestamp now = os_file_last_modified(src);
		if (now > ts) {
			if (ts)
				os_sleep_ms(10);

			ts = now;
			os_library_unload(lib);
			os_file_copy(src, dst);
			lib = os_library_load(dst);
			os_library_symbol(lib, s("tick"), &tick);
		}

		input_update();
		for (OS_Event event; os_event_poll(&event);) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE:
					is_open = false;
					break;

				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					input_feed_key(event.as.key.key_code, event.type == OS_EVENT_TYPE_KEY_PRESS);
					break;

				case OS_EVENT_TYPE_MOUSE_PRESS:
				case OS_EVENT_TYPE_MOUSE_RELEASE:
					input_feed_mouse_button(event.as.mouse_button.button, event.type == OS_EVENT_TYPE_MOUSE_PRESS);
					break;

				case OS_EVENT_TYPE_MOUSE_MOVE:
					input_feed_mouse_motion((double)event.as.mouse_move.x, (double)event.as.mouse_move.y);
					break;
				default:
					break;
			}
		}

		if (tick)
			is_open &= tick(&arena, &input_state);
	}

	gfx_device_destroy(&gfx);

	os_library_unload(lib);
	os_display_shutdown();

	arena_destroy(&arena);
	return 0;
}
