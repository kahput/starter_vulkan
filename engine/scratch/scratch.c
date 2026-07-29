#include "core/arena.h"
#include "os.h"

#include "core/debug.h"
#include "core/logger.h"

#include "utils/input.h"

typedef bool (*TickFn)(Arena *arena, InputState *input);

int main(int32_t argc, const char *argv[]) {
	LOG_INFO("#Hello world! %s", argv[0]);

	ASSERT_MESSAGE(argc > 1, "usage: scratch <shared-library>");

	String8 file = str8_wrap(argv[1]);
	OS_Timestamp ts = os_file_last_modified(file);
	OS_Library lib = os_library_load(file);

	TickFn fn = 0;
	os_library_symbol(lib, s("tick"), &fn);

	Arena arena = arena_make(MiB(16));

	InputState input_state = { 0 };
	input_set_context(&input_state);

	bool is_open = true;
	while (is_open) {
		OS_Timestamp current_ts = os_file_last_modified(file);
		if (current_ts > ts) {
			ts = current_ts;

			os_library_unload(lib);
			lib = os_library_load(file);
		}

		input_update();
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
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

		is_open = fn(&arena, &input_state);
	}

	return 0;
}
