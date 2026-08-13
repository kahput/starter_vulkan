#include "core/arena.h"

#include "os.h"
#include "gfx.h"
#include "gfx/gfx_types.h"

#include "core/debug.h"
#include "core/logger.h"

#include "utils/input.h"

typedef bool (*TickFn)(Arena *permanent, Arena *frame);

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	String8 src = s("libgame.so"), dst = s("libgame_loaded.so");

	OS_Library lib = 0;
	TickFn tick = 0;
	OS_Timestamp ts = 0;

	os_display_startup();
	GFX_Device gfx = { 0 };
	gfx_device_make(&gfx);

	Arena permanenet = arena_make(MiB(128));
	Arena frame = arena_make(MiB(16));

	for (bool is_open = true; is_open;) {
		OS_Timestamp now = os_file_last_modified(src);
		if (now > ts) {
			if (ts)
				os_sleep_ms(100);

			ts = now;
			os_library_unload(lib);
			os_file_copy(src, dst);
			lib = os_library_load(dst);
			os_library_symbol(lib, s("tick"), &tick);
		}

		if (tick)
			is_open &= tick(&permanenet, &frame);

		arena_reset(&frame);
	}

	gfx_device_destroy(&gfx);

	os_library_unload(lib);
	os_display_shutdown();

	arena_destroy(&permanenet);
	arena_destroy(&frame);
	return 0;
}
