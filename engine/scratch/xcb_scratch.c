#include "core/arena.h"
#include "core/cmath.h"
#include "core/debug.h"
#include "core/logger.h"
#include "core/strings.h"
#include "core/input_types.h"

#include "os.h"

typedef struct {
	bool keys[256];
} InputState;

typedef void (*PFN_game_hookup)(void);

int32_t main(int32_t argc, const char *argv[]) {
	os_display_startup();
	OS_Surface *main = os_surface_open(1280, 720, s("Hello world!"), OS_SURFACE_FLAG_RESIZEABLE);
	/* OS_Surface *popup = os_surface_open_with_parent(main, 480, 300, s("Hello world!"), 0); */

	uint64_t timestamp = os_file_last_modified(s("game.so"));
	OS_Library game = os_library_load(s("game/game.so"));
	PFN_game_hookup hookup = NULL;
	os_library_symbol(game, s("game/game_hook"), &hookup);
	if (hookup)
		hookup();

	bool hidden = false;
	bool is_open = true;
	while (is_open) {
		OS_Event event = { 0 };
		while (os_event_poll(&event)) {
			switch (event.type) {
				case OS_EVENT_TYPE_SURFACE_CLOSE: {
					if (event.surface == main)
						is_open = false;
				} break;

				case OS_EVENT_TYPE_KEY_PRESS:
				case OS_EVENT_TYPE_KEY_RELEASE:
					if (event.as.key.key_code == KEY_CODE_ESCAPE)
						is_open = false;

					if (event.as.key.key_code == KEY_CODE_M) {
						if (hookup)
							hookup();
					}
					break;

				case OS_EVENT_TYPE_SURFACE_RESIZE:
				case OS_EVENT_TYPE_SURFACE_FOCUS_GAINED:
				case OS_EVENT_TYPE_SURFACE_FOCUS_LOST:
				case OS_EVENT_TYPE_MOUSE_MOVE:
				case OS_EVENT_TYPE_MOUSE_PRESS:
				case OS_EVENT_TYPE_MOUSE_RELEASE:
				case OS_EVENT_TYPE_MOUSE_SCROLL:

				case OS_EVENT_TYPE_NONE:
					break;
			}
		}

		if (timestamp != os_file_last_modified(s("game.so"))) {
			LOG_INFO("hot-reloading...");
			os_sleep_ms(10);
			os_library_unload(game);
			game = os_library_load(s("game/game.so"));
			os_library_symbol(game, s("game_hook"), &hookup);
			timestamp = os_file_last_modified(s("game/game.so"));
		}
	}

    os_surface_close(main);
    os_display_shutdown();

	return 0;
}
