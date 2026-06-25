#include "core/arena.h"
#include "os.h"

#include "core/debug.h"
#include "core/logger.h"

typedef bool (*TickFn)(Arena *arena);

int main(int32_t argc, const char *argv[]) {
	LOG_INFO("#Hello world! %s", argv[0]);

	ASSERT_MESSAGE(argc > 1, "usage: scratch <shared-library>");

	String8 file = str8_wrap(argv[1]);
	OS_Timestamp ts = os_file_last_modified(file);
	OS_Library lib = os_library_load(file);

	TickFn fn = 0;
	os_library_symbol(lib, s("tick"), &fn);

	Arena arena = arena_make(MiB(16));
	bool is_open = true;
	while (is_open) {
		OS_Timestamp current_ts = os_file_last_modified(file);
		if (current_ts > ts) {
			ts = current_ts;

			os_library_unload(lib);
			lib = os_library_load(file);
		}

		is_open = fn(&arena);
	}

	return 0;
}
