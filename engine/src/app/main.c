#include "core/arena.h"

#include "core/os.h"

#include "gfx.h"
#include "wm.h"
#include "gfx/gfx_types.h"

#include "core/debug.h"
#include "core/logger.h"
#include "core/err.h"

#include "utils/input.h"

typedef bool (*Tick)(Arena *permanent, Arena *frame);

int main(void) {
	logger_set_level(LOG_LEVEL_DEBUG);

	string8 src = s("libgame.so"), dst = s("libgame_loaded.so");

	OS_Library lib = 0;
	Tick tick = 0;
	OS_Timestamp ts = 0;

	wm_startup();

	Arena permanenet = arena_make(MiB(128));
	Arena frame = arena_make(MiB(16));

	for (bool is_open = true; is_open;) {
		OS_Timestamp now = os_file_mtime(src);
		if (now > ts) {
			if (ts)
				os_sleep_ms(100);

			ts = now;
			os_dlclose(lib);
			os_file_copy(src, dst);
			lib = os_dlopen(dst);
			if (lib == 0) break;
			os_dlsym(lib, s("tick"), &tick);
			if (tick == 0) break;
		}

		if (tick)
			is_open &= tick(&permanenet, &frame);

		arena_reset(&frame);
	}

	os_dlclose(lib);
	wm_shutdown();

	arena_destroy(&permanenet);
	arena_destroy(&frame);
	return 0;
}
